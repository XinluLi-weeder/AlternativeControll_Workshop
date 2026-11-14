import unreal
import json

def emit(kind, data):
    unreal.log('INFINITY_AUDIT ' + kind + ' ' + json.dumps(data, default=str))

def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None

def path(obj):
    return obj.get_path_name() if obj else None

emit('settings', {'r.VirtualTextures': unreal.SystemLibrary.get_console_variable_int_value('r.VirtualTextures'),
                  'r.Substrate': unreal.SystemLibrary.get_console_variable_int_value('r.Substrate'),
                  'project': unreal.Paths.get_project_file_path()})
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
assets = registry.get_assets_by_path('/Game/InfinityAssets', recursive=True)
counts = {}
for entry in assets:
    cls = str(entry.asset_class_path.asset_name)
    counts[cls] = counts.get(cls, 0) + 1
emit('counts', counts)
for entry in assets:
    cls = str(entry.asset_class_path.asset_name)
    if cls not in ('Material', 'MaterialInstanceConstant'):
        continue
    asset = entry.get_asset()
    if not asset:
        continue
    data = {'path': path(asset), 'class': cls}
    if cls == 'MaterialInstanceConstant':
        data['parent'] = path(prop(asset, 'parent'))
        data['textures'] = []
        for p in prop(asset, 'texture_parameter_values') or []:
            tex = p.parameter_value
            data['textures'].append({'name': str(p.parameter_info.name), 'texture': path(tex),
                                     'vt': prop(tex, 'virtual_texture_streaming') if tex else None,
                                     'compression': str(prop(tex, 'compression_settings')) if tex else None})
        data['scalars'] = {str(p.parameter_info.name): p.parameter_value for p in prop(asset, 'scalar_parameter_values') or []}
        data['vectors'] = {str(p.parameter_info.name): str(p.parameter_value) for p in prop(asset, 'vector_parameter_values') or []}
    else:
        data['blend_mode'] = str(prop(asset, 'blend_mode'))
        data['expressions'] = []
        try:
            objects = []
            for candidate in unreal.ObjectIterator(unreal.MaterialExpression):
                outer = candidate.get_outer()
                while outer:
                    if outer == asset:
                        objects.append(candidate)
                        break
                    outer = outer.get_outer()
        except Exception as e:
            objects = []
            data['expression_error'] = str(e)
        for obj in objects:
            name = obj.get_class().get_name()
            if 'Texture' in name or 'FunctionCall' in name or 'StaticSwitch' in name or 'ScalarParameter' in name:
                data['expressions'].append({'class': name, 'name': obj.get_name(),
                    'parameter': str(prop(obj, 'parameter_name')), 'sampler': str(prop(obj, 'sampler_type')),
                    'texture': path(prop(obj, 'texture')), 'function': path(prop(obj, 'material_function')),
                    'default': str(prop(obj, 'default_value'))})
        task = unreal.AssetExportTask()
        task.object = asset
        task.filename = unreal.Paths.project_dir() + 'Documentation/Audit_' + asset.get_name() + '.copy'
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        try:
            data['exported'] = unreal.Exporter.run_asset_export_task(task)
        except Exception as e:
            data['export_error'] = str(e)
    emit('material', data)
emit('complete', True)
