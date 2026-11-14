#include "HallwayGame.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "DrawDebugHelpers.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
int32 TagNumber(const AActor* A,const TCHAR* Prefix)
{
    for(const FName T:A->Tags){const FString S=T.ToString();if(S.StartsWith(Prefix))return FCString::Atoi(*S.RightChop(FCString::Strlen(Prefix)));}
    return -1;
}
int32 HallMod4(int32 V){return (V%4+4)%4;}
UStaticMeshComponent* MeshOf(AActor* A){return A?A->FindComponentByClass<UStaticMeshComponent>():nullptr;}
}

AHallwayPuzzle::AHallwayPuzzle(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
const TCHAR* AHallwayPuzzle::ItemName(int32 I)
{
    static const TCHAR* Names[]={TEXT("LOUNGE CHAIR"),TEXT("ENTRY TABLE"),TEXT("COAT STAND"),TEXT("BED"),TEXT("WRITING DESK"),TEXT("DESK CHAIR"),TEXT("ENTRY WARDROBE"),TEXT("ENTRY STORAGE"),TEXT("BEDROOM WARDROBE"),TEXT("DISPLAY CABINET")};
    return I>=0&&I<ItemCount?Names[I]:TEXT("FURNITURE");
}
int32 AHallwayPuzzle::ItemStage(int32 I)
{
    static const int32 Stages[]={0,0,0,1,1,1,0,0,1,-1};
    return I>=0&&I<ItemCount?Stages[I]:-1;
}
int32 AHallwayPuzzle::StageItemCount(int32 S) const
{
    int32 N=0;for(int32 I=0;I<ItemCount;++I)N+=ItemStage(I)==S?1:0;return N;
}
bool AHallwayPuzzle::RoomComplete(int32 S) const
{
    int32 N=0;for(int32 I=0;I<ItemCount;++I)if(ItemStage(I)==S){++N;if(!State.Joined[I])return false;}
    return N>0;
}
const TCHAR* AHallwayPuzzle::FaceName(int32 I)
{
    static const TCHAR* Names[]={TEXT("A CLAY"),TEXT("B SAGE"),TEXT("C BLUE"),TEXT("D MAUVE"),TEXT("E SAND")};return I>=0&&I<5?Names[I]:TEXT("F FLOOR (FIXED)");
}
FTransform AHallwayPuzzle::FaceTransform(int32 S,int32 F,float Q) const
{
    // All axis lines meet at the room's assembly center; wall panels lie on those axes.
    const FVector Axes[]={FVector(0,1,0),FVector(-1,0,0),FVector(1,0,0),FVector(0,-1,0),FVector(0,0,1)};
    return FTransform(FQuat(Axes[F],FMath::DegreesToRadians(90.f*Q)),S==0?EntranceCenter:BedroomCenter);
}
void AHallwayPuzzle::BeginPlay()
{
    Super::BeginPlay();
#if !WITH_EDITOR
    // Keep editor diagnostics in logs rather than over the standalone playtest HUD.
    GAreScreenMessagesEnabled=false;
#endif
    DeviceRunId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if(bStartInBedroom){State.Stage=1;Selected=0;for(int32 I=0;I<ItemCount;++I)if(ItemStage(I)==0)State.Joined[I]=true;}
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/Hallway/M_HallwayOverlay.M_HallwayOverlay"));
    if(Base){SelectedMaterial=UMaterialInstanceDynamic::Create(Base,this);SuccessMaterial=UMaterialInstanceDynamic::Create(Base,this);
        SelectedMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(.12,.55,1));SelectedMaterial->SetScalarParameterValue(TEXT("Strength"),.4f);
        SuccessMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(1,.67,.15));}
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        AActor* A=*It; const int32 S=TagNumber(A,TEXT("RH_STAGE_"));
        if(S==0)EntranceActors.Add(A);
        if(TagNumber(A,TEXT("RH_PROP_"))>=0)PropTransforms.Add(A,A->GetActorTransform());
        if(S==1)BedroomActors.Add(A);
        if(A->ActorHasTag(TEXT("RH_BLACKOUT")))Blackout=A;
        if(A->ActorHasTag(TEXT("RH_CURTAIN"))){Curtain=A;CurtainPosition=A->GetActorLocation();}
        const int32 Source=TagNumber(A,TEXT("RH_SOURCE_"));
        if(Source>=0&&Source<ItemCount){Sources[Source]=A;SourceTransforms[Source]=A->GetActorTransform();if(auto* M=MeshOf(A))M->SetMobility(EComponentMobility::Movable);}
        if(S==1)if(auto* L=A->FindComponentByClass<ULightComponent>()){BedroomLights.Add(L);BedroomIntensities.Add(L->Intensity);L->SetIntensity(0);}
        if(A->ActorHasTag(TEXT("RH_PANEL"))){Panels.Add(A);PanelTransforms.Add(A->GetActorTransform());}
        const int32 Item=TagNumber(A,TEXT("RH_ITEM_"));
        if(Item>=0&&Item<ItemCount&&S>=0&&S<2)
        {
            FHallPart P;P.Actor=A;P.Mesh=MeshOf(A);P.Stage=S;P.Item=Item;P.Face=TagNumber(A,TEXT("RH_FACE_"));P.Goal=TagNumber(A,TEXT("RH_GOAL_"));P.Solved=A->GetActorTransform();
            if(!P.Mesh.IsValid()||P.Face<0||P.Face>=5||P.Goal<0||P.Goal>3)continue;
            P.Mesh->SetMobility(EComponentMobility::Movable);P.Local=P.Solved.GetRelativeTransform(FaceTransform(S,P.Face,P.Goal));Parts.Add(P);
        }
    }
    const FVector StartCamera=bStartInBedroom?BedroomCamera:EntranceCamera;
    const FVector StartCenter=bStartInBedroom?BedroomCenter:EntranceCenter;
    TMap<FString,AActor*> ActorsByName;
    for(TActorIterator<AActor> It(GetWorld());It;++It)ActorsByName.Add(It->GetName(),*It);
    for(auto& P:Parts)
    {
        if(!Sources[P.Item].IsValid()){LayoutValid=false;UE_LOG(LogTemp,Error,TEXT("Missing source for item %d"),P.Item);continue;}
        P.Home=SourceTransforms[P.Item];
        for(const FName Tag:P.Actor->Tags)if(Tag.ToString().StartsWith(TEXT("RH_HOME_")))
        {
            if(AActor** Home=ActorsByName.Find(Tag.ToString().RightChop(8)))P.Home=(*Home)->GetActorTransform();
            else{LayoutValid=false;UE_LOG(LogTemp,Error,TEXT("Missing home actor for %s"),*P.Actor->GetName());}
        }
    }
    for(const auto& P:Parts)
    {
        const FHallPart* Anchor=Parts.FindByPredicate([&P](const FHallPart& Other){return Other.Item==P.Item;});
        const FTransform Expected=P.Solved.GetRelativeTransform(Anchor->Solved)*Anchor->Home;
        if(!Expected.GetLocation().Equals(P.Home.GetLocation(),.5f)||!Expected.GetRotation().Equals(P.Home.GetRotation(),.001f))
        {LayoutValid=false;UE_LOG(LogTemp,Error,TEXT("Non-rigid assembly layout for %s"),*P.Actor->GetName());}
    }
    ViewRotation=bStartInBedroom?FRotator(ActorHasTag(TEXT("RH_HIGH_VIEW"))?-18.f:8.f,90,0):(StartCenter-StartCamera).Rotation();
    Camera=GetWorld()->SpawnActor<ACameraActor>(StartCamera,ViewRotation);
    auto* C=Camera->GetCameraComponent();C->SetFieldOfView(FieldOfView);C->bConstrainAspectRatio=false;
    C->PostProcessSettings.bOverride_AutoExposureMethod=true;C->PostProcessSettings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    C->PostProcessSettings.bOverride_AutoExposureBias=true;C->PostProcessSettings.AutoExposureBias=ExposureCompensation;
    C->PostProcessSettings.bOverride_MotionBlurAmount=true;C->PostProcessSettings.MotionBlurAmount=0;
    C->PostProcessSettings.bOverride_AmbientOcclusionIntensity=true;C->PostProcessSettings.AmbientOcclusionIntensity=.65f;
    if(auto* PC=GetWorld()->GetFirstPlayerController())PC->SetViewTarget(Camera);
    for(const auto& P:Parts)if(P.Mesh.IsValid()&&P.Mesh->GetStaticMesh())
    {
        const FBox Room=P.Stage==0?FBox(FVector(609,-669,-79),FVector(1243,380,539)):FBox(FVector(155,390,-79),FVector(1808,1794,911));
        for(int32 Angle=0;Angle<360;Angle+=5)
        {
            FTransform Face=FaceTransform(P.Stage,P.Face,0);FVector Axis;float R;FaceTransform(P.Stage,P.Face,1).GetRotation().ToAxisAndAngle(Axis,R);Face.SetRotation(FQuat(Axis,FMath::DegreesToRadians(static_cast<float>(Angle))));
            const FBox B=P.Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(P.Actor->GetActorTransform().GetRelativeTransform(FaceTransform(P.Stage,P.Face,P.Goal))*Face);
            if(!Room.IsInsideOrOn(B.Min)||!Room.IsInsideOrOn(B.Max)){LayoutValid=false;UE_LOG(LogTemp,Warning,TEXT("HALLWAY_CLEARANCE_FAIL item=%d face=%d angle=%d"),P.Item,P.Face,Angle);break;}
        }
    }
    UE_LOG(LogTemp,Display,TEXT("HALLWAY_SWEEP_CHECK %s"),LayoutValid?TEXT("PASS"):TEXT("FAIL"));
    CreateTethers();ApplyState();Message=bStartInBedroom?TEXT("BEDROOM / Restore four objects one at a time."):TEXT("ENTRY / Restore five objects to open the bedroom.");MessageTime=8;
    Socket=FUdpSocketBuilder(TEXT("HallwayDevice")).AsNonBlocking().BoundToAddress(FIPv4Address(127,0,0,1)).BoundToPort(19872).WithReceiveBufferSize(65536);
    UE_LOG(LogTemp,Display,TEXT("HALLWAY_READY parts=%d sources=%d device=%s"),Parts.Num(),ItemCount,Socket?TEXT("ready"):TEXT("port busy"));
}
bool AHallwayPuzzle::FaceEnabled(int32 F) const{return F>=0&&F<5&&(State.Stage==1||F==1||F==2||F==4);}
bool AHallwayPuzzle::SelectFace(int32 F){if(!FaceEnabled(F)||Transitioning)return false;Selected=F;Highlight();return true;}
int32 AHallwayPuzzle::FaceFor(AActor* A) const
{
    if(!A||TagNumber(A,TEXT("RH_STAGE_"))!=State.Stage)return -1;
    const int32 I=TagNumber(A,TEXT("RH_ITEM_"));if(I>=0&&I<ItemCount&&State.Joined[I])return -1;
    return TagNumber(A,TEXT("RH_FACE_"));
}
bool AHallwayPuzzle::RotateFace(int32 F,int32 D)
{
    if(Busy()||!FaceEnabled(F)||(D!=1&&D!=-1)||StageProgress()>=StageItemCount(State.Stage))return false;
    History.Add(State);++StageEpoch;Selected=F;Turning=F;TurnTime=0;TurnStart=FaceTransform(State.Stage,F,State.Turns[State.Stage][F]).GetRotation();
    State.Turns[State.Stage][F]=HallMod4(FMath::RoundToInt(State.Turns[State.Stage][F])+D);TurnEnd=FaceTransform(State.Stage,F,State.Turns[State.Stage][F]).GetRotation();++State.Moves;Highlight();return true;
}
const TCHAR* AHallwayPuzzle::PuzzleItemName(int32 I) const
{
    if(bStartInBedroom){switch(I){case 3:return TEXT("BED");case 4:return TEXT("BEDROOM CABINET");case 5:return TEXT("BEDROOM TABLE");case 8:return TEXT("WINDOW AND FRAME");case 9:return TEXT("WARDROBE");default:break;}}
    return ItemName(I);
}
bool AHallwayPuzzle::SetFaceAngles(const TArray<float>& Degrees)
{
    if(Busy()||Degrees.Num()!=5||StageProgress()>=StageItemCount(State.Stage))return false;
    for(float D:Degrees)if(!FMath::IsFinite(D))return false;
    bool Changed=false;
    for(int32 F=0;F<5;++F)if(FaceEnabled(F)&&FMath::Abs(FMath::FindDeltaAngleDegrees(State.Turns[State.Stage][F]*90.f,Degrees[F]))>.01f)Changed=true;
    if(!Changed)return true;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now-LastAnalogMotion>.5){History.Add(State);++State.Moves;}
    LastAnalogMotion=Now;
    for(int32 F=0;F<5;++F)if(FaceEnabled(F))
    {
        const float D=FRotator::ClampAxis(Degrees[F]);
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(State.Turns[State.Stage][F]*90.f,D))>.01f)Selected=F;
        State.Turns[State.Stage][F]=D/90.f;
    }
    Turning=-1;
    ApplyState();return true;
}
void AHallwayPuzzle::BedroomVisibility(bool Visible)
{
    for(auto A:BedroomActors)if(A.IsValid())A->SetActorHiddenInGame(!Visible);
}
void AHallwayPuzzle::SetBedroomLight(float Alpha)
{
    for(int32 I=0;I<BedroomLights.Num();++I)if(BedroomLights[I].IsValid())
    {
        const int32 Prop=TagNumber(BedroomLights[I]->GetOwner(),TEXT("RH_PROP_"));
        BedroomLights[I]->SetIntensity(BedroomIntensities[I]*Alpha*((Prop>=0&&Prop<ItemCount&&ItemStage(Prop)>=0&&!State.Joined[Prop])?0.f:1.f));
    }
}
void AHallwayPuzzle::ApplyState()
{
    BedroomVisibility(State.Stage==1);SetBedroomLight(State.Stage==1?1:0);
    if(Blackout.IsValid())Blackout->SetActorHiddenInGame(State.Stage==1);
    if(Curtain.IsValid())Curtain->SetActorLocation(CurtainPosition+FVector(State.Stage==1?-290:0,0,0));
    RestoreCompletedActors();
    ApplyRoomIsolation();
    for(auto& P:Parts)if(P.Actor.IsValid())
    {
        P.Actor->SetActorHiddenInGame(P.Stage!=State.Stage||State.Joined[P.Item]);
        P.Actor->SetActorTransform(P.Local*FaceTransform(P.Stage,P.Face,State.Turns[P.Stage][P.Face]));
    }
    for(int32 I=0;I<Panels.Num();++I)if(auto* A=Panels[I].Get())
    {
        const int32 S=TagNumber(A,TEXT("RH_STAGE_")),F=TagNumber(A,TEXT("RH_FACE_"));
        const bool Restored=RoomComplete(S);
        A->SetActorHiddenInGame(S>State.Stage||(Restored&&!A->ActorHasTag(TEXT("RH_WALL_DECOR"))));
        A->SetActorTransform(Restored?PanelTransforms[I]:PanelTransforms[I].GetRelativeTransform(FaceTransform(S,F,0))*FaceTransform(S,F,State.Turns[S][F]));
    }
    Highlight();
}
void AHallwayPuzzle::Highlight()
{
    if(SelectedMaterial)SelectedMaterial->SetVectorParameterValue(TEXT("Color"),FaceColor(Selected));
    for(auto& P:Parts)if(P.Mesh.IsValid())P.Mesh->SetOverlayMaterial(P.Item==FlashItem?SuccessMaterial.Get():P.Stage==State.Stage&&!State.Joined[P.Item]&&P.Face==Selected?SelectedMaterial.Get():nullptr);
    for(auto A:Panels)if(A.IsValid())if(auto* M=MeshOf(A.Get()))M->SetOverlayMaterial(!RoomComplete(State.Stage)&&TagNumber(A.Get(),TEXT("RH_STAGE_"))==State.Stage&&TagNumber(A.Get(),TEXT("RH_FACE_"))==Selected?SelectedMaterial.Get():nullptr);
}
int32 AHallwayPuzzle::ReadyItem() const
{
    if(Busy()||GetWorld()->GetTimeSeconds()-LastAnalogMotion<AlignmentHoldSeconds)return -1;
    for(int32 I=0;I<ItemCount;++I)
    {
        if(ItemStage(I)!=State.Stage||State.Joined[I])continue;bool Has=false,Matches=true;
    for(const auto& P:Parts)if(P.Item==I){Has=true;Matches&=FMath::Abs(FMath::FindDeltaAngleDegrees(State.Turns[P.Stage][P.Face]*90.f,P.Goal*90.f))<=FMath::Clamp(AlignmentTolerance,0.f,10.f);}
        if(Has&&Matches)return I;
    }
    return -1;
}
int32 AHallwayPuzzle::StageProgress() const {int32 N=0;for(int32 I=0;I<ItemCount;++I)N+=(ItemStage(I)==State.Stage&&State.Joined[I])?1:0;return N;}
bool AHallwayPuzzle::Assemble()
{
    const int32 I=ReadyItem();if(I<0||!LayoutValid)return false;
    LandingStarts.Empty();for(const auto& P:Parts)LandingStarts.Add(P.Actor.IsValid()?P.Actor->GetActorTransform():P.Solved);
    History.Add(State);Landing=I;LandingTime=0;FlashItem=I;FlashTime=2.6f;
    Message=FString::Printf(TEXT("ASSEMBLING %s"),PuzzleItemName(I));MessageTime=2;Highlight();
    if(auto* PC=GetWorld()->GetFirstPlayerController()){PC->bForceFeedbackEnabled=true;PC->PlayDynamicForceFeedback(.65f,.3f,true,true,true,true,EDynamicForceFeedbackAction::Start);}
    UE_LOG(LogTemp,Display,TEXT("HALLWAY_ASSEMBLED stage=%d item=%d serial=%d"),State.Stage,I,CompletionSerial);return true;
}
bool AHallwayPuzzle::Undo()
{
    if(Busy()||History.IsEmpty())return false;State=History.Pop();++StageEpoch;NextStageDelay=-1;FlashItem=-1;FlashTime=0;ApplyState();return true;
}
bool AHallwayPuzzle::ResetStage()
{
    if(Busy())return false;History.Add(State);++StageEpoch;for(int32 F=0;F<5;++F)State.Turns[State.Stage][F]=0;for(int32 I=0;I<ItemCount;++I)if(ItemStage(I)==State.Stage)State.Joined[I]=false;
    NextStageDelay=-1;FlashItem=-1;FlashTime=0;ApplyState();Message=TEXT("Current room reset.");MessageTime=3;return true;
}
FString AHallwayPuzzle::Hint() const
{
    for(int32 I=0;I<ItemCount;++I)if(ItemStage(I)==State.Stage&&!State.Joined[I])
    {
        FString H=FString(PuzzleItemName(I))+TEXT(": ");for(const auto& P:Parts)if(P.Item==I)H+=FString::Printf(TEXT("%c=%d  "),'A'+P.Face,P.Goal*90);return H+TEXT("then SPACE");
    }
    return TEXT("All furniture in this room restored.");
}
bool AHallwayPuzzle::SetViewAngles(float Y,float P)
{
    if(!FMath::IsFinite(Y)||!FMath::IsFinite(P)||Transitioning)return false;
    ViewRotation=FRotator(FMath::Clamp(P,-75.f,75.f),90+FRotator::NormalizeAxis(Y),0);if(Camera)Camera->SetActorRotation(ViewRotation);return true;
}
void AHallwayPuzzle::StartTransition()
{
    Transitioning=true;TransitionTime=0;TransitStart=Camera->GetActorLocation();TransitRotation=Camera->GetActorRotation();
    Message=TEXT("ENTRY COMPLETE / Welcome home.");MessageTime=TransitionDuration;History.Empty();++StageEpoch;
}
void AHallwayPuzzle::Tick(float Dt)
{
    Super::Tick(Dt);
    PollOSC();
    PollDevice();
    MessageTime=FMath::Max(0.f,MessageTime-Dt);
    UpdateTurn(Dt);
    UpdateLanding(Dt);
    UpdateSuccessEffect(Dt);
    UpdateTransition(Dt);
    UpdateTethers();
}
void AHallwayPuzzle::EndPlay(const EEndPlayReason::Type Reason){if(OSCSocket){OSCSocket->Close();ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(OSCSocket);OSCSocket=nullptr;}if(Socket){Socket->Close();ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);Socket=nullptr;}Super::EndPlay(Reason);}
