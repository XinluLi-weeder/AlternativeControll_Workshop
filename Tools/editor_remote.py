"""Run a script through this project's enabled Unreal Python remote execution."""
import argparse
import sys
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('script', nargs='?')
args = parser.parse_args()
sys.path.insert(0, 'D:/DKU_Academic/UE_5.7/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
import remote_execution
remote = remote_execution.RemoteExecution()
remote.start()
try:
    time.sleep(2)
    nodes = [n for n in remote.remote_nodes if n.get('project_name') == 'RoomShiftDemo']
    if len(nodes) != 1:
        raise RuntimeError(f'Expected one RoomShiftDemo editor: {remote.remote_nodes}')
    remote.open_command_connection(nodes[0]['node_id'])
    command = Path(args.script).read_text(encoding='utf-8') if args.script else "import unreal; print(unreal.Paths.project_dir()); print(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name())"
    result = remote.run_command(command)
    print({k:v for k,v in result.items() if k!='command'})
    if not result['success']:
        raise SystemExit(1)
finally:
    remote.stop()
