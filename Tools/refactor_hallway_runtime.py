"""One-time source refactor with asserted boundaries (source snapshot exists)."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'Plugins/RoomShift/Source/RoomShift/Private'
p=root/'HallwayGame.cpp'
s=p.read_text(encoding='utf-8')
def replace(old,new):
    global s
    assert s.count(old)==1,(old[:100],s.count(old))
    s=s.replace(old,new)

# Completed actors have one owner: immutable authored home transforms.
replace('        if(TagNumber(A,TEXT("RH_FOLLOW_FACE_"))>=0){FollowTransforms.Add(A,A->GetActorTransform());if(auto* M=MeshOf(A))M->SetMobility(EComponentMobility::Movable);}',
'''        if(S==0)EntranceActors.Add(A);
        if(TagNumber(A,TEXT("RH_PROP_"))>=0)PropTransforms.Add(A,A->GetActorTransform());''')
replace('    ApplyState();Message=bStartInBedroom?', '    CreateTethers();ApplyState();Message=bStartInBedroom?')
replace('TEXT("A FRONT"),TEXT("B RIGHT"),TEXT("C LEFT"),TEXT("D BACK"),TEXT("E CEILING")','TEXT("A CLAY"),TEXT("B SAGE"),TEXT("C BLUE"),TEXT("D MAUVE"),TEXT("E SAND")')
replace('    if(Blackout.IsValid())Blackout->SetActorHiddenInGame(State.Stage==1);','    if(Blackout.IsValid())Blackout->SetActorHiddenInGame(State.Stage==1);') # explicit location check
replace('    for(int32 I=0;I<ItemCount;++I)if(Sources[I].IsValid()){Sources[I]->SetActorTransform(SourceTransforms[I]);Sources[I]->SetActorHiddenInGame(!State.Joined[I]);if(auto* M=MeshOf(Sources[I].Get()))M->SetOverlayMaterial(nullptr);}\n    for(TActorIterator<AActor> It(GetWorld());It;++It){const int32 Prop=TagNumber(*It,TEXT("RH_PROP_"));if(Prop>=0&&Prop<ItemCount)It->SetActorHiddenInGame(!State.Joined[Prop]);}',
'''    RestoreCompletedActors();
    ApplyRoomIsolation();''')
replace('History.Add(State);State.Joined[I]=true;Landing=I;', 'History.Add(State);Landing=I;')
replace('FlashTime=2.6f;++CompletionSerial;', 'FlashTime=2.6f;')
replace('PuzzleItemName(I),StageProgress(),StageItemCount(State.Stage));MessageTime=3;Highlight();', 'PuzzleItemName(I),StageProgress()+1,StageItemCount(State.Stage));MessageTime=3;Highlight();')

# Separate network transport and player UI from puzzle rules.
device_start=s.index('void AHallwayPuzzle::PollDevice()')
device_end=s.index('void AHallwayPuzzle::EndPlay',device_start)
device=s[device_start:device_end]
s=s[:device_start]+s[device_end:]
(root/'HallwayDevice.cpp').write_text('''// Local UDP transport only. Puzzle rules remain in HallwayGame/Animation.
#include "HallwayGame.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Camera/CameraActor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
'''+device,encoding='utf-8')
ui_start=s.index('AHallwayPuzzle* AHallwayController::Puzzle()')
ui=s[ui_start:];s=s[:ui_start]
ui=ui.replace('    Text(TEXT("RMB look', '    Text(TEXT("RMB look')
needle='const float X=Left+I*112*Scale;DrawRect('
assert needle in ui
ui=ui.replace(needle,'const float X=Left+I*112*Scale;DrawRect(P->FaceColor(I),X,Top-4*Scale,106*Scale,3*Scale);DrawRect(')
(root/'HallwayUI.cpp').write_text('''// Mouse/keyboard routing and HUD presentation; no puzzle transform ownership.
#include "HallwayGame.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
'''+ui,encoding='utf-8')

# Split Tick into explicitly ordered responsibilities.
start=s.index('void AHallwayPuzzle::Tick(float Dt)')
end=s.index('void AHallwayPuzzle::EndPlay',start)
tick=s[start:end]
turn=tick[tick.index('    if(Turning>=0)'):tick.index('    if(Landing>=0)')]
flash=tick[tick.index('    if(FlashTime>0)'):tick.index('    for(const auto& Entry:FollowTransforms)')]
transition=tick[tick.index('    if(NextStageDelay>=0)'):]
transition=transition[:transition.rfind('}')]
newtick='''void AHallwayPuzzle::Tick(float Dt)
{
    Super::Tick(Dt);
    PollDevice();
    MessageTime=FMath::Max(0.f,MessageTime-Dt);
    UpdateTurn(Dt);
    UpdateLanding(Dt);
    UpdateSuccessEffect(Dt);
    UpdateTransition(Dt);
    UpdateTethers();
}
'''
s=s[:start]+newtick+s[end:]
animation='''// Animation changes transient parts only; finished models stay at authored homes.
#include "HallwayGame.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
namespace {
float AnimSmooth(float V){V=FMath::Clamp(V,0.f,1.f);return V*V*(3-2*V);}
int32 AnimTagNumber(const AActor* A,const TCHAR* Prefix){for(FName T:A->Tags){FString V=T.ToString();if(V.StartsWith(Prefix))return FCString::Atoi(*V.RightChop(FCString::Strlen(Prefix)));}return -1;}
UStaticMeshComponent* AnimMeshOf(AActor* A){return A?A->FindComponentByClass<UStaticMeshComponent>():nullptr;}
}
'''
for name,body in [('UpdateTurn',turn),('UpdateSuccessEffect',flash),('UpdateTransition',transition)]:
    body=body.replace('Smooth(', 'AnimSmooth(').replace('TagNumber(', 'AnimTagNumber(').replace('MeshOf(', 'AnimMeshOf(')
    animation+='void AHallwayPuzzle::'+name+'(float Dt)\n{\n'+body+'}\n'
(root/'HallwayAnimation.cpp').write_text(animation,encoding='utf-8')
p.write_text(s,encoding='utf-8')
print('Split core, animations, UDP and UI; removed post-completion wall-follow loop.')
