import unreal
from pathlib import Path

source=Path(__file__).resolve().parent/'Generated'
root='/Game/RoomShift'
tools=unreal.AssetToolsHelpers.get_asset_tools()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.SyncToBrowser 0')
for filename in list(source.glob('*.png'))+list(source.glob('*.obj')):
    task=unreal.AssetImportTask()
    task.filename=str(filename); task.destination_path=root; task.destination_name=filename.stem
    task.automated=True; task.replace_existing=True; task.save=True
    if filename.suffix=='.obj':
        task.factory=unreal.FbxFactory()
        opts=unreal.FbxImportUI()
        opts.import_mesh=True; opts.import_materials=False; opts.import_textures=False
        opts.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
        opts.automated_import_should_detect_type=False
        opts.static_mesh_import_data.combine_meshes=True
        opts.static_mesh_import_data.generate_lightmap_u_vs=True
        opts.static_mesh_import_data.normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
        task.options=opts
    tools.import_asset_tasks([task])
    if not task.imported_object_paths: raise RuntimeError('Import failed: '+str(filename))
    if filename.stem.endswith('_N'):
        tex=unreal.load_asset(root+'/'+filename.stem)
        tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb',False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)

lib=unreal.MaterialEditingLibrary
for kind, roughness in [('Surface',.4),('Wood',.4),('Fabric',.88),('Plaster',.85)]:
    path=root+'/M_'+kind
    mat=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_'+kind,root,unreal.Material,unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(mat)
    def node(cls,x,y): return lib.create_material_expression(mat,cls,x,y)
    def scalar(name,value,x,y):
        e=node(unreal.MaterialExpressionScalarParameter,x,y)
        e.set_editor_property('parameter_name',name); e.set_editor_property('default_value',value); return e
    tint=node(unreal.MaterialExpressionVectorParameter,-700,0)
    tint.set_editor_property('parameter_name','Tint'); tint.set_editor_property('default_value',unreal.LinearColor(.5,.5,.5,1))
    base=tint
    if kind!='Surface':
        uv=node(unreal.MaterialExpressionTextureCoordinate,-1000,-200)
        uv.set_editor_property('u_tiling',4 if kind=='Fabric' else 1)
        uv.set_editor_property('v_tiling',4 if kind=='Fabric' else 1)
        tex=node(unreal.MaterialExpressionTextureSample,-700,-200); tex.set_editor_property('texture',unreal.load_asset(root+'/T_'+kind))
        lib.connect_material_expressions(uv,'',tex,'UVs')
        mul=node(unreal.MaterialExpressionMultiply,-430,0)
        lib.connect_material_expressions(tint,'',mul,'A'); lib.connect_material_expressions(tex,'RGB',mul,'B'); base=mul
        normal=node(unreal.MaterialExpressionTextureSample,-700,450)
        normal.set_editor_property('texture',unreal.load_asset(root+'/T_'+kind+'_N'))
        normal.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        lib.connect_material_expressions(uv,'',normal,'UVs')
        lib.connect_material_property(normal,'RGB',unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=scalar('Roughness',roughness,-400,180); metal=scalar('Metallic',0,-400,260)
    lib.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(metal,'',unreal.MaterialProperty.MP_METALLIC)
    glow=scalar('Highlight',0,-400,620)
    color=node(unreal.MaterialExpressionVectorParameter,-700,680)
    color.set_editor_property('parameter_name','HighlightColor'); color.set_editor_property('default_value',unreal.LinearColor(1,.45,.08,1))
    emission=node(unreal.MaterialExpressionMultiply,-170,550)
    lib.connect_material_expressions(glow,'',emission,'A'); lib.connect_material_expressions(color,'',emission,'B')
    lib.connect_material_property(emission,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat); unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('ROOMSHIFT_VISUAL_ASSETS_READY')
