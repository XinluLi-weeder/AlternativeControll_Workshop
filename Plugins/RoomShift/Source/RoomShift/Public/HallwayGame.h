#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HallwayGame.generated.h"

class FSocket;
class ACameraActor;
class UStaticMeshComponent;
class ULightComponent;
class UMaterialInstanceDynamic;

struct FHallPart
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    TWeakObjectPtr<UStaticMeshComponent> Tether;
    FTransform Solved, Local, Home;
    int32 Stage=0, Item=0, Face=0, Goal=0;
};
struct FHallState
{
    int32 Stage=0;
    float Turns[2][5]={{0,0,0,0,0},{0,0,0,0,0}}; // Quarter turns, including fractional angles.
    bool Joined[10]={};
    int32 Moves=0;
};

UCLASS()
class ROOMSHIFT_API AHallwayPuzzle : public AActor
{
    GENERATED_BODY()
public:
    AHallwayPuzzle();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere,Category="Hallway|Layout") FVector EntranceCenter=FVector(925,-150,230);
    UPROPERTY(EditAnywhere,Category="Hallway|Layout") bool bStartInBedroom=false;
    UPROPERTY(EditAnywhere,Category="Hallway|Layout") FVector BedroomCenter=FVector(980,1090,380);
    UPROPERTY(EditAnywhere,Category="Hallway|Camera") FVector EntranceCamera=FVector(925,-510,110);
    UPROPERTY(EditAnywhere,Category="Hallway|Camera") FVector BedroomCamera=FVector(980,680,110);
    UPROPERTY(EditAnywhere,Category="Hallway|Camera") FVector Doorway=FVector(925,390,110);
    UPROPERTY(EditAnywhere,Category="Hallway|Camera") float FieldOfView=110.f;
    UPROPERTY(EditAnywhere,Category="Hallway|Camera") float ExposureCompensation=6.f;
    UPROPERTY(EditAnywhere,Category="Hallway|Camera") float TransitionDuration=5.f;
    UFUNCTION(BlueprintCallable,Category="Hallway") bool RotateFace(int32 Face,int32 Direction);
    UFUNCTION(BlueprintCallable,Category="Hallway") bool SetFaceAngles(const TArray<float>& Degrees);
    UPROPERTY(EditAnywhere,Category="Hallway|Controller") float AlignmentTolerance=5.f;
    UPROPERTY(EditAnywhere,Category="Hallway|Controller") float AlignmentHoldSeconds=.2f;
    UFUNCTION(BlueprintCallable,Category="Hallway") bool Assemble();
    UFUNCTION(BlueprintCallable,Category="Hallway") bool Undo();
    UFUNCTION(BlueprintCallable,Category="Hallway") bool ResetStage();
    UFUNCTION(BlueprintCallable,Category="Hallway") bool SetViewAngles(float Yaw,float Pitch);
    bool SelectFace(int32 Face);
    bool FaceEnabled(int32 Face) const;
    bool Busy() const {return Turning>=0||Landing>=0||Transitioning;}
    int32 ReadyItem() const;
    int32 StageProgress() const;
    static constexpr int32 ItemCount=10;
    static int32 ItemStage(int32 Item);
    int32 StageItemCount(int32 Stage) const;
    bool RoomComplete(int32 Stage) const;
    int32 FaceFor(AActor* Actor) const;
    FString Hint() const;
    FHallState State;
    int32 Selected=1;
    int32 CompletionSerial=0;
    int32 StageEpoch=0;
    FString DeviceRunId;
    bool bGameStarted=false;
    bool bMenuOpen=true;
    bool bSettingsOpen=false;
    bool bPhysicalControl=false;
    UFUNCTION(BlueprintCallable) void StartGame();
    UFUNCTION(BlueprintCallable) void SetPhysicalControl(bool Enabled);
    UFUNCTION(BlueprintCallable) void OpenMenu();
    bool ControllerOnline() const;
    FString ControllerStatus() const;
    bool Transitioning=false;
    bool bReducedMotion=false;
    bool bDeviceConnected=false;
    bool bShowControllerDebug=false;
    bool LayoutValid=true;
    FString ControllerSerialLine=TEXT("waiting for ESP32 serial data");
    FString ControllerReplyLine=TEXT("waiting for Unreal response");
    FString Message;
    float MessageTime=0.f;
    static const TCHAR* ItemName(int32 Item);
    const TCHAR* PuzzleItemName(int32 Item) const;
    static const TCHAR* FaceName(int32 Face);
    static FLinearColor FaceColor(int32 Face);
private:
    void PollOSC();
    void ResetOSCReference();
    FSocket* OSCSocket=nullptr;
    double LastRotaryPacket=-100,LastIMUPacket=-100,NextOSCBind=0;
    FString OSCSender;
    bool bOSCBindFailed=false,bRotaryReference=false,bViewReference=false;
    float PhysicalAngles[5]={},AngleOffsets[5]={};
    float SensorYaw=0,SensorPitch=0,PreviousYaw=0,AccumulatedYaw=0;
    float PitchReference=0,ViewBaseYaw=0,ViewBasePitch=0;
    float SmoothYaw=0,SmoothPitch=0;
    double LastViewSample=0,FirstShakePeak=0,LastShake=-100;
    int32 ShakePeaks=0;
    bool bShakeArmed=true;
    FTransform FaceTransform(int32 Stage,int32 Face,float Turn) const;
    double LastAnalogMotion=-100;
    TArray<FTransform> LandingStarts;
    void ApplyState();
    void Highlight();
    void StartTransition();
    void BedroomVisibility(bool Visible);
    void SetBedroomLight(float Alpha);
    void PollDevice();
    void UpdateTurn(float DeltaSeconds);
    void UpdateLanding(float DeltaSeconds);
    void FinishLanding();
    void UpdateSuccessEffect(float DeltaSeconds);
    void UpdateTransition(float DeltaSeconds);
    void CreateTethers();
    void UpdateTethers();
    void RestoreCompletedActors();
    void ApplyRoomIsolation();
    FTransform CurrentFaceTransform(int32 Stage,int32 Face) const;
    TArray<FHallPart> Parts;
    TArray<FHallState> History;
    TWeakObjectPtr<AActor> Sources[ItemCount];
    FTransform SourceTransforms[ItemCount];
    TArray<TWeakObjectPtr<AActor>> BedroomActors;
    TArray<TWeakObjectPtr<ULightComponent>> BedroomLights;
    TArray<float> BedroomIntensities;
    TArray<TWeakObjectPtr<AActor>> Panels;
    TArray<FTransform> PanelTransforms;
    TMap<TWeakObjectPtr<AActor>, FTransform> PropTransforms;
    TArray<TWeakObjectPtr<AActor>> EntranceActors;
    TWeakObjectPtr<AActor> Blackout, Curtain;
    FVector CurtainPosition;
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SelectedMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SuccessMaterial;
    FRotator ViewRotation=FRotator(10,90,0), TransitRotation;
    FVector TransitStart;
    int32 Turning=-1,Landing=-1,FlashItem=-1;
    float TurnTime=0,LandingTime=0,TransitionTime=0,FlashTime=0,NextStageDelay=-1;
    FQuat TurnStart,TurnEnd;
    FSocket* Socket=nullptr;
    TMap<FString,int64> DeviceSequences;
};

UCLASS()
class ROOMSHIFT_API AHallwayController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    AHallwayPuzzle* Puzzle() const;
private:
    bool Looking=false;
    int32 ActiveInputMode=-1;
};

UCLASS()
class ROOMSHIFT_API AHallwayHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    bool HandleClick(float X,float Y);
private:
    float Scale=1,Left=0,Top=0;
};

UCLASS()
class ROOMSHIFT_API AHallwayMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AHallwayMode();
};
