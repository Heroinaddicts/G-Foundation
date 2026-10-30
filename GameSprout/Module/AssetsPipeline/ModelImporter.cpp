#include "AssetImportJob.h"
#include "ThirdParty/ufbx/ufbx.h"
#include <QDataStream>
#include <QVector3D>
#include <cmath>
#include <algorithm>
#include <limits>

namespace {
std::string Name(ufbx_string value) { return std::string(value.data, value.length); }
Json::Value Array(std::initializer_list<double> values) {
    Json::Value array(Json::arrayValue);
    for (double value : values) array.append(value);
    return array;
}
Json::Value Transform(const ufbx_transform & value, double scale) {
    Json::Value result;
    result["Translation"] = Array({value.translation.x * scale, value.translation.y * scale, value.translation.z * scale});
    result["Rotation"] = Array({value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w});
    result["Scale"] = Array({value.scale.x, value.scale.y, value.scale.z});
    return result;
}
Json::Value Matrix(const ufbx_matrix & value, double scale) {
    Json::Value result(Json::arrayValue);
    for (int column = 0; column < 4; ++column) for (int row = 0; row < 3; ++row)
        result.append(value.cols[column].v[row] * (column == 3 ? scale : 1.0));
    return result;
}
std::string NodePath(const ufbx_node * node) {
    if (!node) return "";
    const std::string own = Name(node->name);
    size_t ordinal = 0;
    if (node->parent) for (ufbx_node * sibling : node->parent->children) {
        if (sibling == node) break;
        if (Name(sibling->name) == own) ++ordinal;
    }
    return NodePath(node->parent) + "/" + own + "#" + std::to_string(ordinal);
}
uint64_t ExternalMainID(const Json::Value & meta, eAssetType type) {
    if (meta["SubAssets"].isArray()) for (const Json::Value & entry : meta["SubAssets"]) {
        if (entry.get("Type", 0).asUInt() == static_cast<uint16_t>(type) && entry.get("SourceKey", "").asString() == "main"
            && entry["LocalID"].isUInt64() && entry["LocalID"].asUInt64() != 0) return entry["LocalID"].asUInt64();
    }
    const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(std::to_string(static_cast<uint16_t>(type)) + ":main:0"), QCryptographicHash::Sha256);
    uint64_t id = 0;
    for (int i = 0; i < 8; ++i) id = (id << 8) | static_cast<unsigned char>(hash[i]);
    return id;
}
Json::Value Ref(const std::string & guid, uint64_t id) {
    Json::Value value; value["Guid"] = guid; value["LocalID"] = static_cast<Json::UInt64>(id); return value;
}
std::string NamedKey(const std::string & prefix, const ufbx_element & element,
    const ufbx_element_list & peers) {
    size_t ordinal = 0;
    for (const ufbx_element * peer : peers) {
        if (peer == &element) break;
        if (peer->type == element.type && Name(peer->name) == Name(element.name)) ++ordinal;
    }
    return prefix + Name(element.name) + "#" + std::to_string(ordinal);
}
std::string BlendKey(const ufbx_blend_channel * channel) {
    // The deformer owns a named channel; duplicated names retain their sibling ordinal.
    return Name(channel->name) + "#" + std::to_string(channel->typed_id);
}
QVector3D Vector(ufbx_vec3 value) { return QVector3D(value.x, value.y, value.z); }

bool Mesh(AssetImportJob & job, ufbx_mesh * mesh, const std::string & key, double scale) {
    if (mesh->cache_deformers.count) return job.Fail("Vertex cache deformation is not supported by this runtime asset format");
    if (mesh->skin_deformers.count > 1) return job.Fail("Multiple skin deformers are not supported");
    const ufbx_skin_deformer * skin = mesh->skin_deformers.count ? mesh->skin_deformers[0] : nullptr;
    if (skin && skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR) return job.Fail("Dual-quaternion skinning requires a compatible runtime deformation format");
    const bool recalculate = job.Settings.get("NormalsMode", "Import").asString() == "Calculate";
    const bool normals = job.Settings.get("NormalsMode", "Import").asString() != "None";
    const std::string tangents = job.Settings.get("TangentsMode", "Calculate").asString();
    const bool weld = job.Settings.get("WeldVertices", true).asBool();
    std::vector<QVector3D> faceNormals(mesh->num_faces);
    std::vector<std::vector<size_t>> adjacent(mesh->num_vertices);
    if (recalculate) for (size_t face = 0; face < mesh->num_faces; ++face) {
        const ufbx_face polygon = mesh->faces[face];
        if (polygon.num_indices < 3) continue;
        const QVector3D a = Vector(ufbx_get_vertex_vec3(&mesh->vertex_position, polygon.index_begin));
        const QVector3D b = Vector(ufbx_get_vertex_vec3(&mesh->vertex_position, polygon.index_begin + 1));
        const QVector3D c = Vector(ufbx_get_vertex_vec3(&mesh->vertex_position, polygon.index_begin + 2));
        faceNormals[face] = QVector3D::crossProduct(b - a, c - a);
        for (uint32_t corner = polygon.index_begin; corner < polygon.index_begin + polygon.num_indices; ++corner)
            adjacent[mesh->vertex_indices[corner]].push_back(face);
    }
    const double cosine = std::cos(job.Settings.get("NormalSmoothingAngle", 60.0).asDouble() * 3.141592653589793 / 180.0);
    std::vector<uint32_t> triangles(std::max(size_t(3), mesh->max_face_triangles * 3));
    std::vector<QByteArray> vertices;
    std::map<QByteArray, uint32_t> vertexLookup;
    std::map<uint32_t, std::vector<uint32_t>> parts;
    const size_t uvCount = mesh->uv_sets.count;
    for (size_t face = 0; face < mesh->num_faces; ++face) {
        if (job.Cancelled()) return false;
        const uint32_t count = ufbx_triangulate_face(triangles.data(), triangles.size(), mesh, mesh->faces[face]);
        const uint32_t material = mesh->face_material.count ? mesh->face_material[face] : 0;
        auto & indices = parts[material];
        for (uint32_t triangle = 0; triangle < count; ++triangle) {
            QVector3D position[3]; ufbx_vec2 uv[3]{};
            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t index = triangles[triangle * 3 + corner];
                position[corner] = Vector(ufbx_get_vertex_vec3(&mesh->vertex_position, index));
                if (mesh->vertex_uv.exists) uv[corner] = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
            }
            const QVector3D edge1 = position[1] - position[0], edge2 = position[2] - position[0];
            const double determinant = (uv[1].x - uv[0].x) * (uv[2].y - uv[0].y) - (uv[1].y - uv[0].y) * (uv[2].x - uv[0].x);
            QVector3D tangent, bitangent;
            if (std::abs(determinant) > 1e-15) {
                tangent = (edge1 * (uv[2].y - uv[0].y) - edge2 * (uv[1].y - uv[0].y)) / determinant;
                bitangent = (edge2 * (uv[1].x - uv[0].x) - edge1 * (uv[2].x - uv[0].x)) / determinant;
            }
            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t index = triangles[triangle * 3 + corner];
                const uint32_t controlPoint = mesh->vertex_indices[index];
                QVector3D normal;
                if (normals) {
                    if (recalculate) {
                        const QVector3D reference = faceNormals[face].normalized();
                        for (size_t neighbor : adjacent[controlPoint]) {
                            if (neighbor == face || QVector3D::dotProduct(reference, faceNormals[neighbor].normalized()) >= cosine)
                                normal += faceNormals[neighbor];
                        }
                        normal.normalize();
                    } else if (mesh->vertex_normal.exists) normal = Vector(ufbx_get_vertex_vec3(&mesh->vertex_normal, index)).normalized();
                }
                QVector3D t;
                float sign = 1;
                if (tangents != "None") {
                    if (tangents == "Import" && mesh->vertex_tangent.exists) {
                        t = Vector(ufbx_get_vertex_vec3(&mesh->vertex_tangent, index));
                        if (mesh->vertex_bitangent.exists) sign = QVector3D::dotProduct(QVector3D::crossProduct(normal, t), Vector(ufbx_get_vertex_vec3(&mesh->vertex_bitangent, index))) < 0 ? -1 : 1;
                    } else {
                        t = (tangent - normal * QVector3D::dotProduct(normal, tangent)).normalized();
                        sign = QVector3D::dotProduct(QVector3D::crossProduct(normal, t), bitangent) < 0 ? -1 : 1;
                    }
                }
                QByteArray vertex;
                QDataStream stream(&vertex, QIODevice::WriteOnly);
                stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
                stream << float(position[corner].x() * scale) << float(position[corner].y() * scale) << float(position[corner].z() * scale);
                stream << normal.x() << normal.y() << normal.z() << t.x() << t.y() << t.z() << sign;
                for (size_t channel = 0; channel < uvCount; ++channel) {
                    const auto value = ufbx_get_vertex_vec2(&mesh->uv_sets[channel].vertex_uv, index);
                    stream << float(value.x) << float(value.y);
                }
                ufbx_vec4 color{1,1,1,1};
                if (mesh->vertex_color.exists) color = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
                stream << float(color.x) << float(color.y) << float(color.z) << float(color.w);
                uint32_t bones[4]{}; float weights[4]{};
                if (skin) {
                    const auto value = skin->vertices[controlPoint];
                    float total = 0;
                    for (uint32_t i = 0; i < std::min(value.num_weights, uint32_t(4)); ++i) {
                        const auto weight = skin->weights[value.weight_begin + i];
                        bones[i] = weight.cluster_index; weights[i] = weight.weight; total += weights[i];
                    }
                    if (total > 0) for (float & weight : weights) weight /= total;
                }
                for (uint32_t bone : bones) stream << quint32(bone);
                for (float weight : weights) stream << weight;
                // Retain control-point correspondence for morph target offsets.
                stream << quint32(controlPoint);
                auto existing = weld ? vertexLookup.find(vertex) : vertexLookup.end();
                if (existing == vertexLookup.end()) {
                    if (vertices.size() >= std::numeric_limits<uint32_t>::max()) return job.Fail("Mesh exceeds 32-bit vertex limits");
                    const uint32_t vertexIndex = uint32_t(vertices.size());
                    vertices.push_back(vertex);
                    if (weld) vertexLookup.emplace(vertex, vertexIndex);
                    indices.push_back(vertexIndex);
                } else indices.push_back(existing->second);
            }
        }
    }
    Json::Value metadata;
    metadata["VertexCount"] = static_cast<Json::UInt64>(vertices.size());
    metadata["UVChannels"] = static_cast<Json::UInt64>(uvCount);
    metadata["VertexStride"] = vertices.empty() ? 0 : vertices.front().size();
    metadata["VertexLayout"] = "float32:Position3,Normal3,Tangent4,UV2*N,Color4;uint32:Bone4;float32:Weight4;uint32:ControlPoint";
    metadata["VertexAttributes"] = Json::Value(Json::arrayValue);
    const auto attribute = [&](const char * semantic, const char * format, uint32_t offset, uint32_t channel = 0) {
        Json::Value value;
        value["Semantic"] = semantic;value["Format"] = format;value["Offset"] = offset;value["Channel"] = channel;
        metadata["VertexAttributes"].append(value);
    };
    attribute("Position","Float32x3",0);attribute("Normal","Float32x3",12);attribute("Tangent","Float32x4",24);
    for (uint32_t i=0;i<uvCount;++i) attribute("TexCoord","Float32x2",40+i*8,i);
    attribute("Color","Float32x4",40+uint32_t(uvCount)*8);
    attribute("BoneIndices","UInt32x4",56+uint32_t(uvCount)*8);
    attribute("BoneWeights","Float32x4",72+uint32_t(uvCount)*8);
    attribute("ControlPoint","UInt32",88+uint32_t(uvCount)*8);
    metadata["IndexFormat"] = "UInt32";
    metadata["Settings"] = job.Settings;
    QByteArray payload;
    for (const QByteArray & vertex : vertices) payload.append(vertex);
    metadata["IndexOffset"] = static_cast<Json::UInt64>(payload.size());
    QDataStream output(&payload, QIODevice::Append);
    output.setByteOrder(QDataStream::LittleEndian);
    metadata["SubMeshes"] = Json::Value(Json::arrayValue);
    uint64_t first = 0;
    for (const auto & part : parts) {
        Json::Value section;
        section["MaterialSlot"] = part.first;
        section["FirstIndex"] = static_cast<Json::UInt64>(first);
        section["IndexCount"] = static_cast<Json::UInt64>(part.second.size());
        metadata["SubMeshes"].append(section);
        for (uint32_t index : part.second) output << quint32(index);
        first += part.second.size();
    }
    metadata["IndexCount"] = static_cast<Json::UInt64>(first);
    metadata["Bones"] = Json::Value(Json::arrayValue);
    if (skin) for (ufbx_skin_cluster * cluster : skin->clusters) {
        Json::Value bone;
        bone["NodePath"] = NodePath(cluster->bone_node);
        bone["InverseBindMatrix"] = Matrix(cluster->geometry_to_bone, scale);
        metadata["Bones"].append(bone);
    }
    metadata["BlendShapes"] = Json::Value(Json::arrayValue);
    if (job.Settings.get("ImportBlendShapes", true).asBool()) for (ufbx_blend_deformer * deformer : mesh->blend_deformers)
        for (ufbx_blend_channel * channel : deformer->channels) for (const auto & frame : channel->keyframes) {
            Json::Value shape;
            shape["Name"] = Name(channel->name); shape["ChannelKey"] = BlendKey(channel);
            shape["DefaultWeight"] = channel->weight; shape["TargetWeight"] = frame.target_weight;
            shape["Offsets"] = Json::Value(Json::arrayValue);
            for (size_t i = 0; i < frame.shape->num_offsets; ++i) {
                const auto delta = frame.shape->position_offsets[i];
                Json::Value offset;
                offset["ControlPoint"] = frame.shape->offset_vertices[i];
                offset["Position"] = Array({delta.x * scale, delta.y * scale, delta.z * scale});
                if (i < frame.shape->normal_offsets.count) {
                    const auto normal = frame.shape->normal_offsets[i]; offset["Normal"] = Array({normal.x,normal.y,normal.z});
                }
                shape["Offsets"].append(offset);
            }
            metadata["BlendShapes"].append(shape);
        }
    return job.Emit(eAssetType::Mesh, key, Name(mesh->name), metadata, payload);
}

bool Scene(AssetImportJob & job, ufbx_scene * scene) {
    const double scale = job.Settings.get("ScaleFactor", 1.0).asDouble();
    if (!(scale > 0) || !std::isfinite(scale)) return job.Fail("Invalid model scale");
    if (job.Settings.get("MeshCompression", "Off").asString() != "Off") return job.Fail("Quantized mesh compression is not implemented for the portable format");
    if (job.Settings.get("GenerateLightmapUVs", false).asBool()) return job.Fail("Lightmap UV generation requires a UV unwrapper");
    if (job.Settings.get("AnimationType", "Generic").asString() == "Humanoid") return job.Fail("Humanoid retargeting requires a bone mapping; use Generic for source animation");
    std::map<uint32_t,uint64_t> meshes, materials;
    for (ufbx_mesh * mesh : scene->meshes) {
        const std::string key = "mesh/" + (mesh->instances.count ? NodePath(mesh->instances[0]) : Name(mesh->name));
        meshes[mesh->typed_id] = job.ID(eAssetType::Mesh,key,Name(mesh->name));
        if (!Mesh(job,mesh,key,scale)) return false;
    }
    if (job.Settings.get("ImportMaterials", true).asBool()) for (ufbx_material * material : scene->materials) {
        const std::string key = NamedKey("material/", material->element, scene->elements);
        Json::Value data;
        const auto color = material->pbr.base_color.has_value ? material->pbr.base_color.value_vec3 : material->fbx.diffuse_color.value_vec3;
        data["BaseColor"] = Array({color.x,color.y,color.z,material->pbr.opacity.value_real});
        data["Roughness"] = material->pbr.roughness.value_real;
        data["Metallic"] = material->pbr.metalness.value_real;
        data["Textures"] = Json::Value(Json::arrayValue);
        for (const auto & mapping : material->textures) {
            ufbx_texture * texture = mapping.texture;
            if (texture->type != UFBX_TEXTURE_FILE) return job.Fail("Layered/procedural textures must be baked before model import");
            Json::Value ref;
            ref["Channel"] = Name(mapping.material_prop);
            if (texture->content.size) {
                const std::string textureKey = NamedKey("texture/", texture->element, scene->elements);
                const uint64_t id = job.ID(eAssetType::Texture,textureKey,Name(texture->name));
                if (!job.ProducedKeys.count(std::to_string(static_cast<uint16_t>(eAssetType::Texture)) + ":" + textureKey)) {
                    const QByteArray bytes(static_cast<const char *>(texture->content.data), texture->content.size);
                    if (!ImportTextureBytes(job,bytes,textureKey,Name(texture->name),Json::Value(Json::objectValue))) return false;
                }
                ref["Asset"] = Ref(job.Request.GuidString,id);
            } else {
                const QDir root(QString::fromStdString(job.Request.AssetsRoot));
                const QString parent = QFileInfo(root.filePath(QString::fromStdString(job.Request.SourcePath))).absolutePath();
                const QString resolved = QDir::fromNativeSeparators(QString::fromStdString(Name(texture->filename)));
                QString filename = QDir::fromNativeSeparators(QString::fromStdString(Name(texture->relative_filename)));
                bool relativeToMaterial = false;
                if (QFileInfo(QString::fromStdString(job.Request.SourcePath)).suffix().compare("obj", Qt::CaseInsensitive) == 0
                    && QDir::isRelativePath(filename)) {
                    for (const AssetDependency & dependency : job.Result.Manifest.Dependencies) {
                        if (QFileInfo(QString::fromStdString(dependency.Path)).suffix().compare("mtl", Qt::CaseInsensitive) != 0) continue;
                        const QString materialDirectory = QFileInfo(root.filePath(QString::fromStdString(dependency.Path))).absolutePath();
                        const QString candidate = QDir::cleanPath(QDir(materialDirectory).filePath(filename));
                        if (QFileInfo(candidate).isFile()) { filename = candidate; relativeToMaterial = true; break; }
                    }
                }
                if (!relativeToMaterial) {
                    if (QDir::isAbsolutePath(resolved) && QFileInfo(resolved).isFile()
                        && QFileInfo(resolved).canonicalFilePath().startsWith(root.canonicalPath() + "/")) filename = resolved;
                    else if (filename.isEmpty() || !QDir::isRelativePath(filename)) filename = QFileInfo(resolved).fileName();
                }
                const QString relative = root.relativeFilePath(QDir(parent).filePath(filename));
                if (!job.AddDependency(relative.toStdString())) return false;
                QFile metaFile(root.filePath(relative) + ".meta");
                Json::Value meta;
                if (!metaFile.open(QIODevice::ReadOnly) || !AssetArtifactIO::Parse(metaFile.readAll().toStdString(),meta)
                    || !meta["Guid"].isString() || !Guid(meta["Guid"].asString()).IsValid()) return job.Fail("Texture dependency has no valid Meta: " + relative.toStdString());
                ref["Asset"] = Ref(meta["Guid"].asString(),ExternalMainID(meta,eAssetType::Texture));
            }
            data["Textures"].append(ref);
        }
        materials[material->typed_id] = job.ID(eAssetType::Material,key,Name(material->name));
        if (!job.Emit(eAssetType::Material,key,Name(material->name),data)) return false;
    }
    Json::Value model;
    model["CoordinateSystem"] = "RightHandedYUp";
    model["UnitMeters"] = 1.0;
    model["Nodes"] = Json::Value(Json::arrayValue);
    for (ufbx_node * node : scene->nodes) {
        Json::Value data;
        data["Path"] = NodePath(node); data["Name"] = Name(node->name);
        data["Parent"] = node->parent ? Json::Value(node->parent->typed_id) : Json::Value(-1);
        data["Transform"] = Transform(node->local_transform,scale);
        data["GeometryTransform"] = Transform(node->geometry_transform,scale);
        data["InheritMode"] = static_cast<int>(node->inherit_mode);
        if (node->mesh) data["Mesh"] = Ref(job.Request.GuidString,meshes.at(node->mesh->typed_id));
        data["Materials"] = Json::Value(Json::arrayValue);
        if (job.Settings.get("ImportMaterials", true).asBool()) for (ufbx_material * material : node->materials) data["Materials"].append(Ref(job.Request.GuidString,materials.at(material->typed_id)));
        if (node->camera && job.Settings.get("ImportCameras",false).asBool()) {
            const ufbx_camera & camera = *node->camera;
            Json::Value properties;
            properties["Projection"] = camera.projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE ? "Perspective" : "Orthographic";
            properties["FieldOfViewDegrees"] = Array({camera.field_of_view_deg.x, camera.field_of_view_deg.y});
            properties["OrthographicSize"] = Array({camera.orthographic_size.x * scale, camera.orthographic_size.y * scale});
            properties["AspectRatio"] = camera.aspect_ratio;
            properties["NearPlane"] = camera.near_plane * scale;
            properties["FarPlane"] = camera.far_plane * scale;
            properties["Resolution"] = Array({camera.resolution.x, camera.resolution.y});
            properties["ResolutionIsPixels"] = camera.resolution_is_pixels;
            properties["ProjectionAxes"] = Array({double(camera.projection_axes.right), double(camera.projection_axes.up), double(camera.projection_axes.front)});
            data["CameraProperties"] = properties;
        }
        if (node->light && job.Settings.get("ImportLights",false).asBool()) {
            const ufbx_light & light = *node->light;
            Json::Value properties;
            properties["Type"] = int(light.type);
            properties["Color"] = Array({light.color.x, light.color.y, light.color.z});
            properties["Intensity"] = light.intensity;
            properties["LocalDirection"] = Array({light.local_direction.x, light.local_direction.y, light.local_direction.z});
            properties["Decay"] = int(light.decay);
            properties["AreaShape"] = int(light.area_shape);
            properties["InnerAngleDegrees"] = light.inner_angle;
            properties["OuterAngleDegrees"] = light.outer_angle;
            properties["CastLight"] = light.cast_light;
            properties["CastShadows"] = light.cast_shadows;
            data["LightProperties"] = properties;
        }
        model["Nodes"].append(data);
    }
    if (scene->bones.count) {
        Json::Value skeleton; skeleton["Nodes"] = model["Nodes"];
        if (!job.Emit(eAssetType::Skeleton,"skeleton","Skeleton",skeleton)) return false;
        Json::Value avatar; avatar["AnimationType"] = job.Settings.get("AnimationType","Generic");
        avatar["Skeleton"] = Ref(job.Request.GuidString,job.ID(eAssetType::Skeleton,"skeleton","Skeleton"));
        if (!job.Emit(eAssetType::Avatar,"avatar","Avatar",avatar)) return false;
        model["Skeleton"] = avatar["Skeleton"];
    }
    model["Animations"] = Json::Value(Json::arrayValue);
    if (job.Settings.get("ImportAnimation",true).asBool() && job.Settings.get("AnimationType","Generic").asString() != "None")
        for (ufbx_anim_stack * stack : scene->anim_stacks) {
            if (job.Cancelled()) return false;
            const double duration = stack->time_end - stack->time_begin;
            const double rate = scene->settings.frames_per_second > 0 ? scene->settings.frames_per_second : 30;
            if (!(duration >= 0) || !std::isfinite(duration) || !std::isfinite(rate) || rate > 1000 || duration * rate > 1000000) return job.Fail("Animation sample count exceeds supported limits");
            const uint32_t frames = uint32_t(std::ceil(duration * rate)) + 1;
            QByteArray payload;
            QDataStream output(&payload,QIODevice::WriteOnly);
            output.setByteOrder(QDataStream::LittleEndian); output.setFloatingPointPrecision(QDataStream::SinglePrecision);
            for (uint32_t frame=0; frame<frames; ++frame) {
                if (job.Cancelled()) return false;
                const double time = std::min(duration,double(frame)/rate);
                output << float(time);
                for (ufbx_node * node : scene->nodes) {
                    const auto t = ufbx_evaluate_transform(stack->anim,node,stack->time_begin+time);
                    output << float(t.translation.x*scale) << float(t.translation.y*scale) << float(t.translation.z*scale)
                        << float(t.rotation.x) << float(t.rotation.y) << float(t.rotation.z) << float(t.rotation.w)
                        << float(t.scale.x) << float(t.scale.y) << float(t.scale.z);
                }
                if (job.Settings.get("ImportBlendShapes", true).asBool())
                    for (ufbx_blend_channel * channel : scene->blend_channels)
                        output << float(ufbx_evaluate_blend_weight(stack->anim, channel, stack->time_begin + time));
            }
            Json::Value data;
            data["Duration"] = duration; data["SampleRate"] = rate; data["FrameCount"] = frames;
            data["Tracks"] = Json::Value(Json::arrayValue);
            for (ufbx_node * node : scene->nodes) data["Tracks"].append(NodePath(node));
            data["BlendShapeTracks"] = Json::Value(Json::arrayValue);
            if (job.Settings.get("ImportBlendShapes", true).asBool())
                for (ufbx_blend_channel * channel : scene->blend_channels) data["BlendShapeTracks"].append(BlendKey(channel));
            data["Layout"] = "float32:Time;per-track:Translation3,RotationQuaternion4,Scale3;per-blend-track:Weight1";
            const std::string key = NamedKey("animation/", stack->element, scene->elements);
            if (!job.Emit(eAssetType::AnimationClip,key,Name(stack->name),data,payload)) return false;
            model["Animations"].append(Ref(job.Request.GuidString,job.ID(eAssetType::AnimationClip,key,Name(stack->name))));
        }
    return job.Emit(eAssetType::Model,"main",QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString(),model);
}
}

bool ImportModel(AssetImportJob & job) {
    const QString extension = QFileInfo(QString::fromStdString(job.Request.SourcePath)).suffix().toLower();
    if (extension != "fbx" && extension != "obj") return job.Fail("Model format has no decoder yet: " + extension.toStdString());
    ufbx_load_opts options{};
    const std::string filename = job.Request.AssetsRoot+"/"+job.Request.SourcePath;
    options.filename = {filename.data(),filename.size()};
    options.load_external_files = extension == "obj";
    options.open_file_cb.user = &job;
    options.open_file_cb.fn = [](void * user, ufbx_stream * stream, const char * path, size_t length,
                                const ufbx_open_file_info * info) -> bool {
        AssetImportJob & job = *static_cast<AssetImportJob *>(user);
        try {
            if (info->type != UFBX_OPEN_FILE_OBJ_MTL) return job.Fail("External vertex caches are not supported");
            const QDir root(QString::fromStdString(job.Request.AssetsRoot));
            const QString filename = QDir::fromNativeSeparators(QString::fromUtf8(path, qsizetype(length)));
            const std::string relative = root.relativeFilePath(filename).toStdString();
            QByteArray bytes;
            if (!job.AddDependency(relative, &bytes)) return false;
            return ufbx_open_memory_ctx(stream, info->context, bytes.constData(), size_t(bytes.size()), nullptr, nullptr);
        } catch (const std::exception & error) { return job.Fail(error.what()); }
        catch (...) { return job.Fail("Cannot load external model dependency"); }
    };
    options.generate_missing_normals = true;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = job.Settings.get("UseFileScale",true).asBool() ? 1.0 : 0.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.progress_cb.fn = [](void * user,const ufbx_progress *) {return static_cast<AssetImportJob *>(user)->Cancelled() ? UFBX_PROGRESS_CANCEL : UFBX_PROGRESS_CONTINUE;};
    options.progress_cb.user = &job;
    ufbx_error error{};
    ufbx_scene * scene = ufbx_load_memory(job.SourceBytes.data(),job.SourceBytes.size(),&options,&error);
    if (!scene) {char message[1024]; ufbx_format_error(message,sizeof(message),&error); return job.Fail(message);}
    try {const bool success = Scene(job,scene); ufbx_free_scene(scene); return success;}
    catch (...) {ufbx_free_scene(scene);throw;}
}
