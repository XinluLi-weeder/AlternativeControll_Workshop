"""Add broad, warm fill lighting to Hallyway1 so every puzzle area remains readable."""
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

project=Path(u.Paths.project_dir())
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
assert level.load_level('/Game/Levels/Hallyway1')

backup=project/'Documentation'/('Hallyway1BeforePuzzleLighting-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True,exist_ok=False)
shutil.copy2(project/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')

def tag(actor):
    actor.tags.append('RH_STAGE_1')

def make_rect(label,location,rotation,intensity,color,width,height):
    existing=next((a for a in actors.get_all_level_actors() if a.get_actor_label()==label),None)
    actor=existing or actors.spawn_actor_from_class(u.RectLight,location,rotation)
    actor.set_actor_label(label)
    component=actor.get_component_by_class(u.RectLightComponent)
    component.set_editor_property('intensity',intensity)
    component.set_editor_property('light_color',color)
    component.set_editor_property('source_width',width)
    component.set_editor_property('source_height',height)
    component.set_editor_property('attenuation_radius',1800.0)
    component.set_editor_property('cast_shadows',False)
    component.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
    tag(actor)
    return actor

# The ceiling light keeps a warm room mood. These two broad lights only lift the
# formerly dark wall/edge areas, leaving enough contrast to read the rotating faces.
make_rect('Bedroom_PuzzleFill_Front',u.Vector(980,540,500),u.Rotator(-18,90,0),2200.0,u.Color(255,199,145,255),520.0,320.0)
make_rect('Bedroom_PuzzleFill_Right',u.Vector(1640,1110,440),u.Rotator(0,180,0),1600.0,u.Color(255,176,117,255),350.0,260.0)

for actor in actors.get_all_level_actors():
    if actor.get_actor_label()=='Bedroom_WarmCeiling_3000K':
        light=actor.get_component_by_class(u.RectLightComponent)
        light.set_editor_property('intensity',8000.0)
        light.set_editor_property('source_width',620.0)
        light.set_editor_property('source_height',620.0)
        light.set_editor_property('attenuation_radius',2000.0)

assert level.save_current_level()
u.log('HALLYWAY1_PUZZLE_LIGHTING_COMPLETE backup='+str(backup))
