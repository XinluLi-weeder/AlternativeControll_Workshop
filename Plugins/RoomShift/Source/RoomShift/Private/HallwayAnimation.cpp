// Animation changes transient parts only; finished models stay at authored homes.
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
void AHallwayPuzzle::UpdateLanding(float Dt)
{
    if(Landing<0)return;
    LandingTime+=Dt;
    const float Settle=AnimSmooth(LandingTime/.2f);
    const float Travel=AnimSmooth((LandingTime-.2f)/1.15f);
    const FHallPart* Anchor=Parts.FindByPredicate([this](const FHallPart& Part){return Part.Item==Landing;});
    if(!Anchor)return;
    // First close the small alignment tolerance, then move one rigid assembled group.
    FTransform Group;
    Group.Blend(Anchor->Solved,Anchor->Home,Travel);
    for(int32 Index=0;Index<Parts.Num();++Index)
    {
        FHallPart& Part=Parts[Index];
        if(Part.Item!=Landing||!Part.Actor.IsValid())continue;
        FTransform Pose;
        if(LandingTime<.2f)Pose.Blend(LandingStarts[Index],Part.Solved,Settle);
        else Pose=Part.Solved.GetRelativeTransform(Anchor->Solved)*Group;
        Pose.SetScale3D(Part.Solved.GetScale3D());
        Part.Actor->SetActorTransform(Pose);
    }
    if(LandingTime>=1.35f)FinishLanding();
}

void AHallwayPuzzle::FinishLanding()
{
    const int32 Item=Landing;
    State.Joined[Item]=true;
    ++CompletionSerial;
    Landing=-1;
    ApplyState();
    Message=FString::Printf(TEXT("%s RESTORED  /  %d of %d"),PuzzleItemName(Item),StageProgress(),StageItemCount(State.Stage));
    MessageTime=3.f;
    UE_LOG(LogTemp,Display,TEXT("HALLWAY_LANDED item=%d serial=%d"),Item,CompletionSerial);
    if(RoomComplete(State.Stage))
    {
        if(State.Stage==0&&!bStartInBedroom)NextStageDelay=1.2f;
        else{Message=TEXT("BEDROOM RESTORED / All four objects complete.");MessageTime=20.f;}
    }
}

void AHallwayPuzzle::UpdateTurn(float Dt)
{
    if(Turning>=0)
    {
        TurnTime+=Dt;const float T=AnimSmooth(TurnTime/.85f);FTransform Face=FaceTransform(State.Stage,Turning,0);Face.SetRotation(FQuat::Slerp(TurnStart,TurnEnd,T));
        for(auto& P:Parts)if(P.Stage==State.Stage&&P.Face==Turning&&!State.Joined[P.Item]&&P.Actor.IsValid())P.Actor->SetActorTransform(P.Local*Face);
        for(int32 I=0;I<Panels.Num();++I)if(auto* A=Panels[I].Get())if(AnimTagNumber(A,TEXT("RH_STAGE_"))==State.Stage&&AnimTagNumber(A,TEXT("RH_FACE_"))==Turning)A->SetActorTransform(PanelTransforms[I].GetRelativeTransform(FaceTransform(State.Stage,Turning,0))*Face);
        if(TurnTime>=.85f){Turning=-1;const int32 Ready=ReadyItem();if(Ready>=0){Message=FString::Printf(TEXT("%s ALIGNED / Press SPACE to assemble"),PuzzleItemName(Ready));MessageTime=5;}}
    }
}
void AHallwayPuzzle::UpdateSuccessEffect(float Dt)
{
    if(FlashTime>0)
    {
        const float FlashAge=2.6f-FlashTime;
        FlashTime=FMath::Max(0.f,FlashTime-Dt);
        if(SuccessMaterial)SuccessMaterial->SetScalarParameterValue(TEXT("Strength"),(.5f+.45f*FMath::Sin(FlashTime*9))*FMath::Min(FlashTime,1.f));
        if(FlashItem>=0&&Sources[FlashItem].IsValid())
        {
            AActor* Restored=Sources[FlashItem].Get();
            const FVector RingCenter=Restored->GetActorLocation()-FVector(0,0,80);
            DrawDebugCircle(GetWorld(),RingCenter,45.f+FlashAge*110.f,64,FColor(255,176,48),false,0.f,0,4.f,FVector(1,0,0),FVector(0,1,0),false);
            DrawDebugCircle(GetWorld(),RingCenter,FMath::Max(12.f,20.f+FlashAge*80.f),64,FColor(255,232,150),false,0.f,0,2.f,FVector(1,0,0),FVector(0,1,0),false);
            Restored->SetActorScale3D(SourceTransforms[FlashItem].GetScale3D());
        }
        if(Camera)
        {
            Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
        }
        if(FlashTime==0)
        {
            if(FlashItem>=0&&Sources[FlashItem].IsValid())
            {
                if(auto* M=AnimMeshOf(Sources[FlashItem].Get()))M->SetOverlayMaterial(nullptr);
                Sources[FlashItem]->SetActorScale3D(SourceTransforms[FlashItem].GetScale3D());
            }
            if(Camera)Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
            FlashItem=-1;Highlight();
        }
    }
}
void AHallwayPuzzle::UpdateTransition(float Dt)
{
    if(NextStageDelay>=0){NextStageDelay-=Dt;if(NextStageDelay<=0){NextStageDelay=-1;StartTransition();}}
    if(Transitioning)
    {
        TransitionTime+=Dt;const float T=FMath::Clamp(TransitionTime/FMath::Max(.1f,TransitionDuration),0.f,1.f);
        FVector Position;
        if(T<.55f)Position=FMath::Lerp(TransitStart,Doorway, AnimSmooth(T/.55f));
        else Position=FMath::Lerp(Doorway,BedroomCamera,AnimSmooth((T-.55f)/.45f));
        Camera->SetActorLocation(Position);Camera->SetActorRotation(FQuat::Slerp(TransitRotation.Quaternion(),FRotator(12,90,0).Quaternion(),AnimSmooth(T*2)));
        if(Curtain.IsValid())Curtain->SetActorLocation(CurtainPosition+FVector(-290*AnimSmooth(T/.45f),0,0));
        if(T>=.50f){BedroomVisibility(true);if(Blackout.IsValid())Blackout->SetActorHiddenInGame(true);for(int32 I=0;I<ItemCount;++I)if(ItemStage(I)==1&&Sources[I].IsValid())Sources[I]->SetActorHiddenInGame(true);for(TActorIterator<AActor> It(GetWorld());It;++It){const int32 Prop=AnimTagNumber(*It,TEXT("RH_PROP_"));if(Prop>=0&&Prop<ItemCount&&ItemStage(Prop)==1)It->SetActorHiddenInGame(true);}}
        // Keep the rear inset panel out of the doorway until the camera has crossed it.
        if(T>=.50f&&T<.66f)for(auto A:Panels)if(A.IsValid()&&AnimTagNumber(A.Get(),TEXT("RH_STAGE_"))==1&&AnimTagNumber(A.Get(),TEXT("RH_FACE_"))==3)A->SetActorHiddenInGame(true);
        SetBedroomLight(AnimSmooth((T-.50f)/.45f));
        if(T>=1){State.Stage=1;Transitioning=false;Selected=0;ViewRotation=FRotator(12,90,0);History.Empty();++StageEpoch;ApplyState();Message=TEXT("BEDROOM / Restore four objects one at a time. A-E are available.");MessageTime=7;UE_LOG(LogTemp,Display,TEXT("HALLWAY_BEDROOM_UNLOCKED"));}
    }
}
