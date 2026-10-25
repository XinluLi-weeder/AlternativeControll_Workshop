"""One-time visual pass for the generated Studio map. Does not touch the legacy map."""
import unreal as u
import math
import ast
from pathlib import Path

levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
assert levels.load_level('/Game/RoomShift/RoomShiftStudio')
root='/Game/RoomShift/Studio'
lib=u.MaterialEditingLibrary
tools=u.AssetToolsHelpers.get_asset_tools()

# Reuse the source layout data and pure transform functions without running the generator.
tree=ast.parse(Path(__file__).with_name('create_studio_scene.py').read_text())
scope={'math':math}
for node in tree.body:
    if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id in ['SPECS','CENTERS','GOALS'] for t in node.targets):
        exec(compile(ast.Module(body=[node],type_ignores=[]),'layout','exec'),scope)
    if isinstance(node,ast.FunctionDef) and node.name in ['mul','rotate','inv_turn']:
        exec(compile(ast.Module(body=[node],type_ignores=[]),'layout','exec'),scope)

for a in actors.get_all_level_actors():
    tags=[str(t) for t in a.tags]
    furniture=next((int(t[13:]) for t in tags if t.startswith('RS_FURNITURE_')),None)
    face=next((int(t[8:]) for t in tags if t.startswith('RS_FACE_')),None)
    if furniture is None: continue
    name,path,center,yaw,scale,axis,faces=scope['SPECS'][furniture]
    b=u.load_asset(path).get_bounding_box()
    q=(0,0,math.sin(math.radians(yaw)/2),math.cos(math.radians(yaw)/2))
    offset=scope['rotate'](q,(-(b.min.x+b.max.x)*.5*scale,-(b.min.y+b.max.y)*.5*scale,-b.min.z*scale))
    goal=[center[i]+offset[i]+(180 if i==2 else 0) for i in range(3)]
    inverse=scope['inv_turn'](face,scope['GOALS'][furniture][face])
    pivot=scope['CENTERS'][face]
    delta=scope['rotate'](inverse,[goal[i]-pivot[i] for i in range(3)])
    a.set_actor_location_and_rotation(u.Vector(*[pivot[i]+delta[i] for i in range(3)]),u.Quat(*scope['mul'](inverse,q)).rotator(),False,True)

path=root+'/M_StudioFloor'
m=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_StudioFloor',root,u.Material,u.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
world=lib.create_material_expression(m,u.MaterialExpressionWorldPosition,-800,0)
mask=lib.create_material_expression(m,u.MaterialExpressionComponentMask,-620,0)
for channel,value in [('r',True),('g',True),('b',False),('a',False)]: mask.set_editor_property(channel,value)
assert lib.connect_material_expressions(world,'',mask,'')
divide=lib.create_material_expression(m,u.MaterialExpressionDivide,-440,0); divide.set_editor_property('const_b',250.)
assert lib.connect_material_expressions(mask,'',divide,'A')
tex=lib.create_material_expression(m,u.MaterialExpressionTextureSample,-240,0)
tex.set_editor_property('texture',u.load_asset('/Game/ArchVis/Textures/T_Wood'))
assert lib.connect_material_expressions(divide,'',tex,'UVs')
assert lib.connect_material_property(tex,'RGB',u.MaterialProperty.MP_BASE_COLOR)
rough=lib.create_material_expression(m,u.MaterialExpressionConstant,-200,220); rough.set_editor_property('r',.57)
lib.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
lib.recompile_material(m); u.EditorAssetLibrary.save_loaded_asset(m)

rug=tools.create_asset('MI_StudioRug',root,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew()) if not u.EditorAssetLibrary.does_asset_exist(root+'/MI_StudioRug') else u.load_asset(root+'/MI_StudioRug')
rug.set_editor_property('parent',u.load_asset('/Game/RoomShift/M_Fabric'))
lib.set_material_instance_vector_parameter_value(rug,'Tint',u.LinearColor(.42,.37,.29,1))
lib.set_material_instance_scalar_parameter_value(rug,'Roughness',.95)
u.EditorAssetLibrary.save_loaded_asset(rug)
for a in actors.get_all_level_actors():
    c=a.get_component_by_class(u.StaticMeshComponent)
    if not c: continue
    label=a.get_actor_label()
    if label.startswith('FloorPlank'): c.set_material(0,m)
    if label=='Rug': c.set_material(0,rug)
assert levels.save_current_level()
u.log('ROOMSHIFT_STUDIO_VISUAL_TUNED')
