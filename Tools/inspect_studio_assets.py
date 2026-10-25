import unreal, json, os
root = unreal.Paths.project_dir()
report = {}
for folder in ['/Game/ArchVis/Mesh', '/Game/OldFurniturePack_01/Meshes']:
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True):
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            b = asset.get_bounding_box()
            report[path] = {'min': [b.min.x,b.min.y,b.min.z], 'max': [b.max.x,b.max.y,b.max.z],
                'materials': [str(m.material_interface.get_path_name()) if m.material_interface else '' for m in asset.static_materials]}
with open(os.path.join(root,'Documentation/StudioAssets.json'),'w') as f:
    json.dump(report,f,indent=2)
for cls in ['GeometryScript_AssetUtils','GeometryScript_MeshBooleans','GeometryScript_NewAssetUtils']:
    c=getattr(unreal,cls,None)
    if c:
        for name in dir(c):
            if any(s in name for s in ['copy_mesh_from_static','apply_mesh_plane_cut','create_new_static_mesh_asset']):
                unreal.log(cls+'.'+name+' '+str(getattr(c,name).__doc__))
unreal.log('STUDIO_ASSET_INSPECT_DONE')
