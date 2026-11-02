#!/usr/bin/env python3
"""Create a self-contained playable project. Never overwrite an existing directory."""
import argparse
import copy
import hashlib
import json
import math
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCRIPT_GUID = '1349119f706a432ea716f963a18c2001'
CAMERA_GUID = '1349119f706a432ea716f963a18c2002'
RIG_GUID = '1349119f706a432ea716f963a18c2010'
CHARACTER_MATERIAL_GUID = '1349119f706a432ea716f963a18c2092'
CLIP_GUIDS = {name: f'1349119f706a432ea716f963a18c20{i:02x}' for i, name in enumerate(('Idle','Walk','Run','Jump'), 32)}
BONES = [('Root',None,[0,0,0]),('Torso','Root',[0,.25,0]),('Head','Root',[0,.75,0]),('ArmL','Root',[-.5,.3,0]),('ArmR','Root',[.5,.3,0]),('LegL','Root',[-.2,-.5,0]),('LegR','Root',[.2,-.5,0])]

def write(root, path, value):
    path = root / path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=4) + '\n' if not isinstance(value,str) else value, encoding='utf-8')

def local_id(kind,key='main'):
    return int.from_bytes(hashlib.sha256(f'{kind}:{key}:0'.encode()).digest()[:8], 'big')

def ref(guid, ident):
    return {'Guid':guid,'LocalID':ident}

def meta(root,path,guid,kind):
    write(root,'Assets/'+path+'.meta', {'Version':1,'Guid':guid,'ImporterType':kind,'Path':path,'ImportSettings':{},'SubAssets':[]})

def value(kind, payload):
    return {'Kind':kind,'Value':payload}

def transform(position=None,scale=None):
    return {'Type':'GameSproutRuntime.Transform','Position':position or [0,0,0], 'Rotation':[0,0,0,1], 'Scale':scale or [1,1,1]}

def skeleton_fbx():
    # An original, deliberately tiny FBX skeleton. Visual geometry uses SDK primitives.
    objects=[];connections=[];ids={name:100+i for i,(name,_,_) in enumerate(BONES)}
    for i,(name,parent,position) in enumerate(BONES):
        x,y,z=position
        objects.append(f'''    Model: {ids[name]}, "Model::{name}", "LimbNode" {{
        Version: 232
        Properties70: {{
            P: "Lcl Translation", "Lcl Translation", "", "A",{x},{y},{z}
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }}
    }}
    NodeAttribute: {200+i}, "NodeAttribute::{name}", "LimbNode" {{ TypeFlags: "Skeleton" }}''')
        connections.extend([f'    C: "OO",{ids[name]},{ids[parent] if parent else 0}', f'    C: "OO",{200+i},{ids[name]}'])
    return '''; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
 FBXVersion: 7400
}
GlobalSettings: { Version: 1000
 Properties70: {
 P: "UpAxis", "int", "Integer", "",1
 P: "UpAxisSign", "int", "Integer", "",1
 P: "FrontAxis", "int", "Integer", "",2
 P: "FrontAxisSign", "int", "Integer", "",-1
 P: "CoordAxis", "int", "Integer", "",0
 P: "CoordAxisSign", "int", "Integer", "",1
 P: "UnitScaleFactor", "double", "Number", "",100
 }
}
Objects: {
'''+ '\n'.join(objects)+'\n}\nConnections: {\n'+'\n'.join(connections)+'\n}\n'

def create(output):
    output=Path(output).expanduser().absolute()
    output.mkdir(parents=True,exist_ok=False)
    try:
        write(output,'Info/project.json',{'Project':{'name':'Third Person Playground'}})
        write(output,'Info/GameLogic.json',{'Version':1,'Guid':'1349119f706a432ea716f963a18c2000'})
        for name,guid in [('ThirdPersonCharacter',SCRIPT_GUID),('ThirdPersonCamera',CAMERA_GUID)]:
            path='Scripts/'+name+'.hpp'
            destination=output/'Assets'/path;destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/'Assets'/path,destination);meta(output,path,guid,'Script')
        # Direct velocity control presses into walls every fixed step. A dedicated
        # zero-friction capsule must not turn that pressure into upward support.
        write(output,'Assets/Character.physicmaterial',{'Version':1,'Friction':0,'Restitution':0,
              'FrictionCombine':'Minimum','RestitutionCombine':'Minimum'})
        meta(output,'Character.physicmaterial',CHARACTER_MATERIAL_GUID,'PhysicsMaterial')
        write(output,'Assets/CharacterRig.fbx',skeleton_fbx());meta(output,'CharacterRig.fbx',RIG_GUID,'Model')
        # ufbx's synthetic scene root has an empty name; every track includes sibling identity.
        paths=['/#0']+['/#0/Root#0' + ('' if name=='Root' else '/'+name+'#0') for name,_,_ in BONES]
        parents=[-1,0]+[1]*(len(BONES)-1)
        for name,guid in CLIP_GUIDS.items():
            frames=[]
            for index in range(31):
                phase=index/30;poses=[{'Translation':[0,0,0],'Rotation':[0,0,0,1],'Scale':[1,1,1]}]
                for bone,_,position in BONES:
                    position=list(position);angle=0
                    if name in ('Walk','Run'):
                        amplitude=.55 if name=='Walk' else .95
                        if bone in ('ArmL','LegR'):angle=math.sin(phase*2*math.pi)*amplitude
                        if bone in ('ArmR','LegL'):angle=-math.sin(phase*2*math.pi)*amplitude
                        if bone=='Torso':position[1]+=.035*abs(math.sin(phase*2*math.pi))
                    elif name=='Idle' and bone=='Torso':position[1]+=.015*math.sin(phase*2*math.pi)
                    elif name=='Jump' and bone.startswith('Arm'):angle=-.8
                    poses.append({'Translation':position,'Rotation':[math.sin(angle/2),0,0,math.cos(angle/2)],'Scale':[1,1,1]})
                frames.append({'Time':phase,'Poses':poses})
            write(output,f'Assets/Animations/{name}.anim',{'Version':1,'Duration':1,'SampleRate':30,'Tracks':paths,'Parents':parents,'Frames':frames})
            meta(output,f'Animations/{name}.anim',guid,'AnimationClip')
        nodes=[];next_id=1
        def node(name,parent=0,position=None,scale=None,components=()):
            nonlocal next_id
            ident=next_id;next_id+=1
            data={'ID':ident,'ParentID':parent,'Name':name,'Active':True,'Components':[transform(position,scale),*copy.deepcopy(components)]}
            for component in data['Components']:component['ID']=next_id;next_id+=1
            nodes.append(data);return data
        def comp(data,suffix):return next(c for c in data['Components'] if c['Type']=='GameSproutRuntime.'+suffix)
        def mesh(kind):return [{'Type':'GameSproutRuntime.Mesh','Mesh':ref('00000000000000000000000000000001',kind)}, {'Type':'GameSproutRuntime.MeshRenderer','Visible':True,'Materials':[]}]
        ground=node('Ground',position=[0,-.25,0],scale=[24,.5,24],components=mesh(1)+[{'Type':'GameSproutRuntime.BoxCollider'}])
        node('Wall',position=[-4,1,-3],scale=[1,2,7],components=mesh(1)+[{'Type':'GameSproutRuntime.BoxCollider'}])
        node('Obstacle',position=[2,.5,-3],scale=[2,1,2],components=mesh(1)+[{'Type':'GameSproutRuntime.BoxCollider'}])
        player=node('Player',position=[0,1.04,0],components=[{'Type':'GameSproutRuntime.Rigidbody','Settings':{'MotionType':'Dynamic','Constraints':56}}, {'Type':'GameSproutRuntime.CapsuleCollider','Settings':{'Material':ref(CHARACTER_MATERIAL_GUID,local_id(19))},'Shape':{'Radius':.4,'Height':2,'Axis':'Y'}}])
        visual=node('Visual',player['ID'])
        rig=node('Rig',visual['ID'],components=[{'Type':'GameSproutRuntime.Animator','Skeleton':ref(RIG_GUID,local_id(30,'skeleton')),'Speed':1,'PlayOnStart':False,'Loop':True,'RootMotionMode':'Disabled','Bindings':[]}])
        bone_ids={};binding=[{'Path':paths[0],'ObjectFileID':rig['ID']}]
        sizes={'Torso':[.65,.65,.35],'Head':[.4,.4,.4],'ArmL':[.2,.65,.25],'ArmR':[.2,.65,.25],'LegL':[.22,.8,.28],'LegR':[.22,.8,.28]}
        for index,(name,parent,position) in enumerate(BONES):
            bone=node(name,bone_ids[parent] if parent else rig['ID'],position=position)
            bone_ids[name]=bone['ID'];binding.append({'Path':paths[index+1],'ObjectFileID':bone['ID']})
            if name!='Root':node(name+' Mesh',bone['ID'],scale=sizes[name],components=mesh(1))
        comp(rig,'Animator')['Bindings']=binding
        camera=node('Main Camera',position=[0,3,5.5],components=[{'Type':'GameSproutRuntime.Camera','Settings':{'Projection':'Perspective','VerticalFovRadians':1.0471975512,'OrthographicHeight':10,'NearPlane':.1,'FarPlane':200,'CullingMask':4294967295}}])
        fields={key:value('Component',{'ComponentFileID':comp(data,kind)['ID']}) for key,data,kind in [('Body',player,'Rigidbody'),('Collider',player,'CapsuleCollider'),('Animator',rig,'Animator')]}
        fields.update({key:value('SceneObject',{'ObjectFileID':data['ID']}) for key,data in [('ViewCamera',camera),('Visual',visual)]})
        fields.update({name+'Clip':value('Resource',ref(guid,local_id(14))) for name,guid in CLIP_GUIDS.items()})
        for data,source,name,values in [(player,SCRIPT_GUID,'ThirdPersonCharacter',fields),(camera,CAMERA_GUID,'ThirdPersonCamera',{'Target':value('SceneObject',{'ObjectFileID':player['ID']}),'Height':value('Number',.5)})]:
            data['Components'].append({'ID':next_id,'Type':'GameSproutRuntime.ScriptComponent','InstanceID':1,'ScriptTypeID':source,'Source':ref(source,1),'Enabled':True,'Fields':{name+'::'+key:val for key,val in values.items()}});next_id+=1
        scene={'Version':1,'Name':'Third Person Playground','MainCameraID':camera['ID'],'NextObjectID':next_id,'Objects':nodes}
        write(output,'Assets/Playground.scene',scene);meta(output,'Playground.scene','1349119f706a432ea716f963a18c2090','Scene')
        selected={player['ID']}
        for item in nodes:
            if item['ParentID'] in selected:selected.add(item['ID'])
        prefab_nodes=copy.deepcopy([item for item in nodes if item['ID'] in selected])
        prefab_nodes[0]['ParentID']=0
        for component in prefab_nodes[0]['Components']:
            if component['Type']=='GameSproutRuntime.ScriptComponent':
                component['Fields']['ThirdPersonCharacter::ViewCamera']=value('SceneObject',None)
        prefab={'Version':1,'Format':'GameSproutPrefab','Base':None,'NestedInstances':[],
                'Data':{'Version':1,'Name':'Player','MainCameraID':0,'NextObjectID':next_id,'Objects':prefab_nodes}}
        write(output,'Assets/Player.prefab',prefab);meta(output,'Player.prefab','1349119f706a432ea716f963a18c2091','Prefab')
        write(output,'UserSettings/EditorState.json',{'LastScenePath':'Playground.scene'})
        write(output,'.gitignore','Library/\nUserSettings/\n')
    except Exception:
        shutil.rmtree(output)
        raise
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    print(create(args.output))
