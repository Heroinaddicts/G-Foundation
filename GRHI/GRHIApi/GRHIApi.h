#ifndef __GRHIApi_h__
#define __GRHIApi_h__

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
struct HWND__;
#endif

namespace GRHI {
    // 一张可供使用者选择的物理显卡的信息，不代表已经创建的渲染设备。
    struct AdapterInfo {
        // 不透明的物理显卡标识；调用方仅保存、比较和传回，不解析格式。
        // 保存的 ID 在下次运行时必须重新匹配，不能假定永久有效。
        const std::string Id;
        const std::string Name; // 用于界面显示的显卡名称，不作为唯一标识。
        // 保存显卡标识和显示名称，不创建 GPU 资源。
        AdapterInfo(const std::string& id, const std::string & name) : Id(id), Name(name) {}
    };

    // 空字符串保留给默认显卡选择策略，不出现在枚举结果中。
    inline const std::string DefaultDeviceAdapterID{};

    class ITexture;
    class ISampler;
    class IShader;
    class IGraphicsPipeline;
    class ICommandBuffer;
    class IRenderPass;

    // 像素在内存中的通道排列，以及 Shader 读取和写入时的颜色编码规则。
    // UNorm 将无符号整数归一化到 [0, 1]；sRGB 的 RGB 通道采样时解码为线性值，
    // 用作颜色目标时将线性输出编码为 sRGB，Alpha 通道不进行 sRGB 转换。
    enum class TextureFormat : uint8_t {
        Unknown,          // 未知或未就绪；不能用它创建纹理或管线。
        RGBA8_UNorm,      // 每像素 4 字节，依次为 R、G、B、A，各通道 8 位，线性编码。
        RGBA8_UNorm_sRGB, // RGBA 排列，RGB 使用 sRGB 编码，适合通常的彩色图片。
        BGRA8_UNorm,      // 每像素 4 字节，依次为 B、G、R、A，各通道 8 位，线性编码。
        BGRA8_UNorm_sRGB  // BGRA 排列，RGB 使用 sRGB 编码，也可能用于窗口画面。
    };

    // 纹理允许的使用方式，可通过 | 组合；实际组合还须得到设备支持。
    enum class TextureUsage : uint8_t {
        None = 0,           // 没有用途，不能据此创建可用纹理。
        Sampled = 1 << 0,   // Shader 可以读取、采样这张纹理，例如读取图片颜色。
        ColorTarget = 1 << 1 // 可以作为绘制输出，例如把场景画进这张纹理。
    };

    // 合并用途，例如 Sampled | ColorTarget 表示既可采样又可接收绘制结果。
    constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) {
        return static_cast<TextureUsage>(
            static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    // 取用途的交集；判断某标志时，将结果与 TextureUsage::None 比较。
    constexpr TextureUsage operator&(TextureUsage left, TextureUsage right) {
        return static_cast<TextureUsage>(
            static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    // 创建纹理所需的固定属性；首版为二维、单层、单 mip、单采样纹理。
    struct TextureDesc {
        uint32_t Width = 0;    // 像素宽度，创建时必须大于 0。
        uint32_t Height = 0;   // 像素高度，创建时必须大于 0。
        TextureFormat Format = TextureFormat::RGBA8_UNorm; // 每个像素的格式。
        TextureUsage Usage = TextureUsage::Sampled;      // 创建后允许进行哪些操作。
    };

    // CPU 内存中的初始像素数据；像素排列必须与 TextureDesc::Format 一致。
    // 行按从上到下排列，每行像素从左到右排列，不包含 PNG/JPG 文件头等编码数据。
    struct TextureInitialData {
        const void* Pixels = nullptr; // 第一行像素的地址，仅在 CreateTexture 调用期间借用。
        size_t DataSize = 0;          // 从 Pixels 开始可读取的总字节数，不是像素数量。
        // 0 表示紧密排列；否则必须至少能容纳一行像素。
        // DataSize 至少为 (Height - 1) * 有效 RowPitch + 一行像素字节数。
        size_t RowPitch = 0;
    };

    // 采样位置落在像素之间时，如何从纹理像素取得颜色。
    enum class FilterMode : uint8_t {
        Nearest, // 使用最近的像素，边缘清晰，常用于像素风图片。
        Linear   // 对邻近像素插值，放大或缩小时过渡更平滑。
    };

    // UV 超过 [0, 1] 时，如何决定实际采样的位置。
    enum class AddressMode : uint8_t {
        ClampToEdge,  // 超出部分使用边缘颜色，适合显示单张图片。
        Repeat,       // 重复平铺纹理，例如 1.2 对应下一个周期的 0.2。
        MirroredRepeat // 每个周期交替镜像，减轻平铺接缝。
    };

    // 纹理的读取规则；同一张纹理可以配合不同采样器使用。
    struct SamplerDesc {
        FilterMode MinFilter = FilterMode::Linear; // 图片被缩小时使用的过滤方式。
        FilterMode MagFilter = FilterMode::Linear; // 图片被放大时使用的过滤方式。
        AddressMode AddressU = AddressMode::ClampToEdge; // UV 横向越界的处理方式。
        AddressMode AddressV = AddressMode::ClampToEdge; // UV 纵向越界的处理方式。
    };

    // Shader 在一次绘制中负责的阶段，首版仅提供顶点和片元阶段。
    enum class ShaderStage : uint8_t {
        Vertex,  // 计算顶点的裁剪空间位置，以及传给片元阶段的 UV 等数据。
        Fragment // 计算片元的输出颜色，例如按 UV 读取图片颜色。
    };

    // 编译产物的格式，不是供使用者选择的设备后端。
    enum class ShaderCodeFormat : uint8_t {
        Unknown, // 未指定有效代码格式，不能用于创建 Shader。
        SPIRV,   // Vulkan 使用的 SPIR-V 二进制代码。
        DXBC,    // DirectX 字节码；能否使用取决于实际 SDL 设备支持。
        DXIL,    // DirectX 中间语言二进制代码，供兼容的 Direct3D 12 设备使用。
        MSL,     // Metal Shading Language 文本，由 Metal 编译器编译。
        Metallib // Apple 工具链生成的 Metal Shader 库二进制。
    };

    // 某一目标格式的 Shader 代码及其元数据，不是 Slang 源文件描述。
    struct ShaderCode {
        ShaderCodeFormat Format = ShaderCodeFormat::Unknown; // 本段代码的格式。
        const void* Data = nullptr; // 代码的首地址，仅在 CreateShader 调用期间借用。
        size_t DataSize = 0;        // 代码的字节数，二进制不能用 strlen 计算。
        const char* EntryPoint = "main"; // 编译结果中需要执行的入口函数名称。
        // 纹理与采样器配对的槽位数量，从 0 连续编号。
        // 必须匹配该编译产物；首版不支持 uniform / storage 资源。
        // 首版仅片元阶段支持采样；顶点阶段的 SamplerCount 必须为 0。
        uint32_t SamplerCount = 0;
    };

    // 一个逻辑 Shader 阶段的创建描述，可同时携带多个平台的编译结果。
    struct ShaderDesc {
        ShaderStage Stage = ShaderStage::Vertex; // 此 Shader 属于顶点还是片元阶段。
        // 提供同一阶段、同一逻辑 Shader 的多个编译版本。
        // GRHI 根据 SDL 实际设备选择兼容版本，无兼容版本时创建失败。
        // 所有版本须满足 SDL 对目标格式的绑定规则。
        // MSL 数据须以 '\0' 结尾，DataSize 包括结尾字节。
        const ShaderCode* Codes = nullptr;
        size_t CodeCount = 0; // Codes 数组的元素数量，创建时必须大于 0。
    };

    // Draw 提供的顶点按什么方式组合成图形，不决定顶点的位置。
    enum class PrimitiveType : uint8_t {
        TriangleList,  // 每 3 个顶点组成一个独立三角形；显示图片时使用。
        TriangleStrip, // 前 3 个顶点组成首个三角形，之后每个顶点连接出一个三角形。
        LineList,      // 每 2 个顶点组成一条独立线段。
        LineStrip,     // 相邻顶点依次连接成连续折线。
        PointList      // 每个顶点作为一个点图元绘制。
    };

    // 是否跳过指定朝向的三角形；朝向由 FrontFace 定义。
    enum class CullMode : uint8_t {
        None, // 两个朝向都绘制，图片显示的默认选择。
        Front, // 丢弃被判定为正面的三角形，只绘制背面。
        Back   // 丢弃被判定为背面的三角形，只绘制正面。
    };

    // 定义三角形的哪种顶点环绕顺序算正面，与相机或世界对象的朝向无关。
    enum class FrontFace : uint8_t {
        CounterClockwise, // 逆时针环绕的三角形视为正面。
        Clockwise         // 顺时针环绕的三角形视为正面。
    };

    // 片元输出颜色如何与目标中已有的颜色组合；src 为新颜色，dst 为已有颜色。
    enum class BlendMode : uint8_t {
        Opaque, // 关闭混合，直接写入新颜色；即使 Alpha 为 0 也不会自动透明。
        // 普通透明混合，适用于 RGB 尚未乘 Alpha 的图片。
        // RGB: src * srcAlpha + dst * (1 - srcAlpha)。
        // Alpha: srcAlpha + dstAlpha * (1 - srcAlpha)。
        Alpha,
        // 预乘透明混合，要求输入 RGB 已乘自身 Alpha。
        // RGB: src + dst * (1 - srcAlpha)；Alpha 与 Alpha 模式相同。
        PremultipliedAlpha
    };

    // 绘制时使用的一组固定规则，与要绘制的具体图片资源分开创建。
    struct GraphicsPipelineDesc {
        IShader* VertexShader = nullptr;   // 顶点 Shader，必须是 Vertex 阶段。
        IShader* FragmentShader = nullptr; // 片元 Shader，必须是 Fragment 阶段。
        TextureFormat ColorFormat = TextureFormat::Unknown; // 必须匹配绘制目标格式。
        PrimitiveType Primitive = PrimitiveType::TriangleList; // 顶点的组合方式。
        CullMode Cull = CullMode::None;                      // 三角形剔除方式。
        FrontFace Winding = FrontFace::CounterClockwise;     // 三角形的正面定义。
        BlendMode Blend = BlendMode::Opaque;                 // 颜色混合方式。
        // 首版单颜色目标、无深度测试、无顶点输入。
        // 顶点 Shader 通过顶点编号生成顶点，无需 IBuffer。
    };

    // 浮点颜色；本 API 的清屏颜色按线性值提供，常规范围为 [0, 1]。
    struct Color {
        float R = 0.0f; // 红色分量。
        float G = 0.0f; // 绿色分量。
        float B = 0.0f; // 蓝色分量。
        float A = 1.0f; // 不透明度，0 为完全透明，1 为完全不透明。
    };

    // 将 Shader 输出的标准化位置映射到目标上的某个像素矩形。
    struct Viewport {
        // 相对于渲染目标左上角的像素坐标，不是桌面坐标或 Qt 逻辑坐标。
        float X = 0.0f;        // 矩形左边距。
        float Y = 0.0f;        // 矩形上边距。
        float Width = 0.0f;    // 矩形宽度，设置时必须大于 0。
        float Height = 0.0f;   // 矩形高度，设置时必须大于 0。
        float MinDepth = 0.0f; // 深度映射范围下限；首版无深度测试，通常保持 0。
        float MaxDepth = 1.0f; // 深度映射范围上限，通常保持 1。
    };

    // 不拥有纹理；Token 仅供 GRHI 识别目标，不是原生对象指针或资产 GUID。
    // 只能使用 GRHI 返回的视图，不得自行生成或修改 Token，0 表示无效。
    struct ColorTargetView {
        uint64_t Token = 0;
    };

    // 向 Surface 获取本次绘制的窗口画面时，区分可绘制、暂时跳过和真实错误。
    enum class AcquireFrameResult : uint8_t {
        Ready,       // 已取得有效画面，可以记录绘制，之后必须提交命令。
        Unavailable, // 暂时没有可画的画面，例如最小化；可跳过本次刷新。
        Failed       // 获取失败，检查所属 Device::GetLastError。
    };

    // 某次 AcquireFrame 返回的借用画面信息，不是需要 delete 的独立资源。
    struct SurfaceFrame {
        ColorTargetView ColorTarget; // 本次命令可写入的窗口颜色目标。
        uint32_t Width = 0;           // 实际像素宽度，可能不同于 Qt 逻辑宽度。
        uint32_t Height = 0;          // 实际像素高度，高 DPI 屏幕上尤其要区分。
        TextureFormat Format = TextureFormat::Unknown; // 本帧目标格式，管线须与之匹配。
    };

    // 一段 RenderPass 开始时，如何处理目标原有的像素内容。
    enum class LoadOp : uint8_t {
        Clear, // 用 RenderPassDesc::ClearColor 清空目标，再绘制新内容。
        // 只用于已经写入有效内容的普通纹理；窗口帧不保证保留之前的内容。
        Load,
        DontCare // 不保留也不清空原内容，内容未定义；适合随后完整覆盖目标。
    };

    // 一段 RenderPass 结束时，是否保存绘制结果。
    enum class StoreOp : uint8_t {
        Store,   // 保存结果，后续需要呈现或采样时使用。
        DontCare // 无需保留结果，不能依赖之后读取或呈现这些像素。
    };

    // 一段绘制过程的目标和开始/结束规则，不包含具体 Draw 命令。
    struct RenderPassDesc {
        ColorTargetView ColorTarget;  // 从 Surface 或 ITexture 取得的有效目标视图。
        LoadOp Load = LoadOp::Clear;   // 开始时清屏、保留或忽略原内容。
        StoreOp Store = StoreOp::Store; // 结束时是否保留结果；窗口呈现时使用 Store。
        Color ClearColor;              // 仅 Load 为 Clear 时使用。
    };

    // 所有资源由创建它的 Device 分配 ID，并由调用方 delete。
    // 同一 Device 生命周期内 ID 非 0 且不重复，不作为跨运行的资产标识。
    // 析构须释放底层资源；Release 可重复调用，不删除包装对象。
    // 资源至少保持有效至引用它的命令提交或取消，且先于 Device 销毁。
    class IResource {
    public:
        // 允许通过 IResource* 删除具体资源；具体实现的析构须释放底层 GPU 资源。
        virtual ~IResource() {}
        // 由具体实现传入 Device 分配的标识，仅记录 ID，不分配 GPU 内存。
        explicit IResource(const uint64_t guid) : _Guid(guid) {}

        // 禁止复制资源对象，避免多个包装对象共用一个 ID 和同一份所有权。
        IResource(const IResource&) = delete;
        // 禁止用另一个资源覆盖当前对象的 ID 和底层资源所有权。
        IResource& operator=(const IResource&) = delete;

        // 释放底层资源并使其不可再用于绘制；可重复调用，之后仍须 delete 包装对象。
        // 调用前须保证引用此资源的命令均已提交或取消。
        virtual void Release() = 0;

        const uint64_t _Guid; // 当前 Device 内的运行时资源标识，创建后不变。
    };

    // GPU 数据缓冲区的公共接口；首版保留类型，尚未开放创建和读写功能。
    class IBuffer : public IResource {
    public:
        // 通过 IBuffer* 删除时，由具体实现释放缓冲区。
        virtual ~IBuffer() {}
        // 初始化缓冲区的资源 ID；具体 GPU 内存由 Device 的实现负责分配。
        explicit IBuffer(const uint64_t guid) : IResource(guid) {}
    };

    // GPU 上的二维像素资源，可以存放图片，也可以按用途作为离屏绘制目标。
    class ITexture : public IResource {
    public:
        // 删除纹理包装对象，并由具体实现释放纹理资源。
        virtual ~ITexture() {}
        // 初始化纹理的资源 ID，不在接口构造函数中上传像素。
        explicit ITexture(const uint64_t guid) : IResource(guid) {}

        // 返回纹理创建时的像素宽度，不是它在窗口中显示的宽度。
        virtual uint32_t GetWidth() const = 0;
        // 返回纹理创建时的像素高度，不随承载窗口大小改变。
        virtual uint32_t GetHeight() const = 0;
        // 返回像素通道排列及颜色编码，例如 RGBA8_UNorm_sRGB。
        virtual TextureFormat GetFormat() const = 0;

        // 获取把这张纹理作为绘制输出时使用的视图，不新建或复制纹理。
        // 仅 ColorTarget 用途的纹理可取得有效视图，其他纹理返回无效视图。
        // 视图有效至纹理 Release 或销毁，不得用于引用此纹理的同一 Pass 采样。
        virtual ColorTargetView GetColorTargetView() const = 0;
    };

    // 独立的纹理采样规则对象，不存储图片像素。
    class ISampler : public IResource {
    public:
        // 删除采样器包装对象，并由具体实现释放底层采样器。
        virtual ~ISampler() {}
        // 初始化采样器 ID；实际过滤和寻址规则由 CreateSampler 提供。
        explicit ISampler(const uint64_t guid) : IResource(guid) {}
    };

    // 已为当前 Device 创建的某个阶段的 GPU 程序，不直接代表完整绘制管线。
    class IShader : public IResource {
    public:
        // 删除 Shader 包装对象，并由具体实现释放底层程序资源。
        virtual ~IShader() {}
        // 初始化 Shader ID；代码格式选择和程序创建由 Device 负责。
        explicit IShader(const uint64_t guid) : IResource(guid) {}

        // 返回此程序是顶点 Shader 还是片元 Shader，供组装管线时检查。
        virtual ShaderStage GetStage() const = 0;
    };

    // 已创建的固定绘制规则；可重复用于多次绘制和不同图片。
    class IGraphicsPipeline : public IResource {
    public:
        // 删除管线包装对象，并由具体实现释放底层管线。
        virtual ~IGraphicsPipeline() {}
        // 初始化管线 ID；Shader 和绘制状态由 CreateGraphicsPipeline 提供。
        explicit IGraphicsPipeline(const uint64_t guid) : IResource(guid) {}

        // 返回管线要求的颜色输出格式，必须与当前 RenderPass 的目标一致。
        virtual TextureFormat GetColorFormat() const = 0;
    };

    // 借用对象，由 ICommandBuffer 持有，不允许调用方 delete。
    // 仅在 BeginRenderPass 与 EndRenderPass 之间有效，不支持嵌套 Pass。
    class IRenderPass {
    public:
        // 供具体实现构造；使用方通过 ICommandBuffer::BeginRenderPass 获取对象。
        IRenderPass() = default;
        // 供所属命令对象清理包装对象；使用方应调用 EndRenderPass，不能自行 delete。
        virtual ~IRenderPass() {}

        // Pass 绑定唯一的命令和绘制状态，不能复制成另一段绘制过程。
        IRenderPass(const IRenderPass&) = delete;
        // 禁止覆盖另一个活动 Pass 的状态和所属关系。
        IRenderPass& operator=(const IRenderPass&) = delete;

        // 为后续 Draw 选择绘制管线；本方法不立即执行绘制，也不接管对象所有权。
        // Pipeline 的颜色格式必须与当前目标相同。
        virtual void SetPipeline(IGraphicsPipeline* pipeline) = 0;
        // 设置后续 Draw 映射到的像素矩形；不会移动或调整 Qt 的控件。
        virtual void SetViewport(const Viewport& viewport) = 0;
        // 将纹理和采样器绑定到片元 Shader 的 slot 槽位，不复制或重新上传图片。
        // slot 从 0 开始，须小于片元 Shader 的 SamplerCount；texture 须支持 Sampled。
        // texture、sampler 须有效且属于当前 Device，不得采样本 Pass 的输出纹理。
        virtual void SetFragmentTextureSampler(uint32_t slot, ITexture* texture, ISampler* sampler) = 0;
        // 记录一次非索引绘制；须先绑定管线及 Shader 需要的全部采样槽位。
        // vertexCount：每个实例使用的顶点数，画覆盖屏幕的大三角形时为 3。
        // instanceCount：绘制多少个实例，显示单张图片时为 1。
        // firstVertex / firstInstance：顶点和实例的起始编号，通常都为 0。
        // 这里只记录命令，调用 Device::Submit 后才安排 GPU 执行。
        virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    };

    // 由 Device 管理的一次性命令对象，不允许调用方 delete。
    // 同一条命令须在获取它的线程记录、获取 SurfaceFrame 和提交/取消。
    class ICommandBuffer {
    public:
        // 供具体实现构造；使用方通过 Device::AcquireCommandBuffer 获取对象。
        ICommandBuffer() = default;
        // 由 Device 在 Submit 或成功 Cancel 时清理；使用方不能自行 delete。
        virtual ~ICommandBuffer() {}

        // 每条命令只能提交或取消一次，禁止复制以免重复提交同一组 GPU 操作。
        ICommandBuffer(const ICommandBuffer&) = delete;
        // 禁止覆盖尚未提交或取消的命令对象。
        ICommandBuffer& operator=(const ICommandBuffer&) = delete;

        // 开始向 desc.ColorTarget 绘制，并按 desc.Load 决定是否清屏。
        // 同一命令不能同时存在多个活动 Pass；目标须有效且属于同一 Device。
        // 失败返回 nullptr；成功时初始 Viewport 覆盖整个目标。
        virtual IRenderPass* BeginRenderPass(const RenderPassDesc& desc) = 0;
        // 结束本命令的当前活动 Pass，并按 StoreOp 处理结果；pass 随即失效。
        // 不提交 GPU 命令，也不直接呈现窗口；结束后可继续开始其他 Pass。
        virtual void EndRenderPass(IRenderPass* pass) = 0;
    };

#if defined(_WIN32)
    typedef ::HWND__* GRHIWindowHandle; // Windows 的原生 HWND，不是 Qt 的 QWindow*。
#elif defined(__APPLE__) && TARGET_OS_OSX
    typedef void* GRHIWindowHandle; // NSView*
#else
    typedef unsigned long GRHIWindowHandle; // 当前按 X11 Window ID 解释，尚不描述 Wayland 对象。
#endif

    // 宿主原生窗口与 GPU 呈现画面之间的连接，不负责管理场景、相机或图片。
    class ISurface {
    public:
        // 供具体实现构造；使用方通过 Device::CreateSurface 创建 Surface。
        ISurface() = default;
        // 删除 Surface 包装对象，并由具体实现解除窗口绑定和释放呈现资源。
        virtual ~ISurface() {}

        // 禁止复制同一个窗口的呈现连接，避免重复释放和重复绑定。
        ISurface(const ISurface&) = delete;
        // 禁止覆盖已有的窗口绑定和呈现资源。
        ISurface& operator=(const ISurface&) = delete;

        // 解除呈现连接，使本 Surface 不再可用于获取画面。
        // 调用方 delete；析构须释放资源。原生窗口先于 Surface 创建、后于其销毁。
        // Release 可重复调用，不删除包装对象，不销毁宿主的原生窗口。
        virtual void Release() = 0;

        // 返回当前窗口颜色目标的格式，创建图片绘制管线时使用。
        // 未就绪时返回 Unknown；呈现参数变化可能导致格式变化，调用方须匹配管线。
        virtual TextureFormat GetColorFormat() const = 0;

        // 将纹理等比例居中画到窗口，清除留白区域并提交呈现；由宿主定时调用。
        // 内部复用采样器、Shader 和管线，管理命令、窗口帧、RenderPass 和提交。
        // texture 须有效、支持 Sampled 且属于同一 Device；只在本次调用期间借用。
        // 在宿主窗口线程调用，不创建线程或定时器。成功或窗口暂不可绘制时返回 true；
        // 失败返回 false，详细信息通过所属 Device::GetLastError 查询。
        virtual bool Draw(ITexture* texture, const Color& clearColor = Color{}) = 0;

        // 将本次可绘制的窗口画面写入 frame，并绑定到指定 command。
        // 可能等待可用的窗口画面；不是窗口坐标或 Qt 控件尺寸的查询方法。
        // 在创建宿主窗口的线程、且 command 无活动 Pass 时调用。
        // Ready: frame 提供实际像素尺寸，目标仅可由本 command 写入，不能采样。
        // Unavailable: 如最小化，清空 frame，不视为错误，且未取得目标。
        // Failed: 清空 frame，错误通过所属 Device::GetLastError 获取。
        // 目标仅有效至 command 提交，不由调用方 delete 或 Release。
        // 取得任何有效帧后必须 Submit，不能 Cancel；对应 Surface 须存活至提交。
        virtual AcquireFrameResult AcquireFrame(
            ICommandBuffer* command, SurfaceFrame& frame) = 0;
    };

    // 某张显卡上的渲染设备；后端由 SDL 自动选择，可创建多个 Surface 和资源。
    class IDevice {
    public:
        // 供具体实现构造；使用方通过 GRHIApi::CreateDevice 创建真实设备。
        IDevice() = default;
        // 删除设备包装对象，并由具体实现释放底层设备；此前须清理所有子对象。
        virtual ~IDevice() {}

        // 禁止复制设备，避免多份对象共同拥有同一底层 GPU 设备。
        IDevice(const IDevice&) = delete;
        // 禁止通过赋值覆盖设备及其资源归属关系。
        IDevice& operator=(const IDevice&) = delete;

        // 释放底层设备，可重复调用，不删除包装对象；之后仍须由调用方 delete。
        // 释放后不能再创建资源、记录命令或绘制。
        // 先提交/取消所有命令，并销毁所属 Surface 和资源，再释放 Device。
        virtual void Release() = 0;

        // 以宿主提供的原生句柄创建呈现连接，失败返回 nullptr。
        // handle 是 HWND / NSView* / X11 Window ID，不接受 QWidget* 或 QWindow*。
        // 在创建宿主窗口的线程调用；Surface 不接管窗口所有权，返回对象由调用方 delete。
        virtual ISurface * CreateSurface(GRHIWindowHandle const handle) = 0;

        // 按 desc 创建二维纹理，失败返回 nullptr，成功对象由调用方 delete。
        // 描述及其指针数据仅在创建调用期间借用。
        // 初始像素在返回前复制到 GRHI 持有的内存并安排上传；之后的绘制可直接使用。
        // initialData 为空时内容未定义，读取前须先写入或清屏。
        virtual ITexture* CreateTexture(const TextureDesc& desc, const TextureInitialData* initialData = nullptr) = 0;
        // 按 desc 创建过滤和 UV 寻址规则；可与多张图片搭配使用。
        // 失败返回 nullptr，成功对象由调用方 delete，描述仅在调用期间借用。
        virtual ISampler* CreateSampler(const SamplerDesc& desc) = 0;
        // 从 desc.Codes 中选择当前设备可用的代码版本并创建对应阶段的程序。
        // 无兼容代码或创建失败时返回 nullptr，不会因此切换或重新创建设备后端。
        // 成功对象由调用方 delete，代码、入口名称和数组仅在调用期间借用。
        virtual IShader* CreateShader(const ShaderDesc& desc) = 0;
        // 将两个阶段的 Shader 和绘制规则组合为可重复使用的管线。
        // Shader 须有效、阶段正确且属于本 Device；ColorFormat 须是受支持的有效格式。
        // 失败返回 nullptr，成功对象由调用方 delete；描述仅在调用期间借用。
        // 成功后 Pipeline 独立于用于创建它的两个 Shader 对象，可删除这两个 Shader。
        virtual IGraphicsPipeline* CreateGraphicsPipeline(
            const GraphicsPipelineDesc& desc) = 0;

        // 获取一条尚未记录内容的一次性命令，失败返回 nullptr。
        // 同一命令及其使用的 Surface、资源和目标必须属于同一 Device。
        // 成功后必须 Submit 或 Cancel，不得自行 delete，也不能遗留到 Device 销毁。
        virtual ICommandBuffer* AcquireCommandBuffer() = 0;
        // 将记录的绘制安排到 GPU 执行，返回是否成功；不代表 GPU 已完成执行。
        // 先结束所有 Pass。无论成功或失败均消耗并释放命令包装对象及其帧视图。
        // 已取得的窗口画面随提交安排呈现，不提供额外 Present 调用。
        virtual bool Submit(ICommandBuffer* command) = 0;
        // 丢弃尚未提交的命令，其中的绘制不会执行，返回是否成功。
        // 仅无活动 Pass、且未取得任何有效 SurfaceFrame 时可取消。
        // 成功消耗并释放命令；失败保留命令，由调用方结束 Pass 或改为提交。
        virtual bool Cancel(ICommandBuffer* command) = 0;

        // 返回此 Device 及其子对象操作的最近错误说明；正常完成操作时清空。
        // 资源创建、获取帧或提交返回失败时，可立即调用它查看原因。
        // 无效绘制操作须标记命令失败，不得传入底层；Submit 返回 false 并回收命令。
        // 引用有效至下一次相关操作或 Device 销毁；Get* 查询不清空错误。
        virtual const std::string& GetLastError() const = 0;
    };

    // GRHI 的入口，负责枚举物理显卡和创建渲染设备，不直接记录绘制命令。
    class GRHIApi {
    public:
        // 删除 API 实现；调用前须先删除由它创建的全部 Device 及其子对象。
        virtual ~GRHIApi() {}

        // 返回初始化时缓存的 GPU 信息，不创建运行设备。
        // 使用本地 SDL 扩展统一枚举物理显卡，不选择图形后端。
        // 后端与显卡的兼容性在 CreateDevice 时检查，不按名称匹配显卡。
        // 列表在 GRHIApi 存活期间保持不变，引用有效至其销毁。
        virtual const std::vector<AdapterInfo> & GetAdapterList() const = 0;

        // 返回最近一次 CreateDevice 的错误，创建成功时为空。
        // 具体 Device 内的纹理或绘制错误应查询该 Device::GetLastError。
        // 返回引用有效至下一次创建设备或 API 销毁。
        virtual const std::string & GetLastError() const = 0;

        // 仅选择物理显卡；图形后端由 SDL 自动选择，调用方不指定。
        // adapterId 为空时按默认策略选显卡；非空时须与枚举出的 Id 精确匹配。
        // 使用当前接口枚举出的 Id 创建设备；失败返回 nullptr。
        // 返回对象由调用方 delete，并先于所属 GRHIApi 销毁。
        virtual IDevice* CreateDevice(const std::string& adapterId = DefaultDeviceAdapterID) = 0;
    };
};

#endif //__GRHIApi_h__
