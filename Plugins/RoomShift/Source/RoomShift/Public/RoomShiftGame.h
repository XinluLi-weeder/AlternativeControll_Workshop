#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "RoomShiftGame.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UStaticMesh;
class FSocket;
constexpr int32 RoomShiftMaxFaces = 5;
constexpr int32 RoomShiftMaxFurniture = 6;

struct FRoomPiece
{
    UStaticMeshComponent* Mesh = nullptr;
    FTransform Local;
    int32 Furniture = 0;
    int32 Face = 0;
};
struct FRoomSnapshot
{
    int32 Turns[RoomShiftMaxFaces] = {};
    bool Joined[RoomShiftMaxFurniture] = {};
    int32 Moves = 0;
};

UCLASS()
class ROOMSHIFT_API ARoomShiftPuzzle : public AActor
{
    GENERATED_BODY()
public:
    ARoomShiftPuzzle();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="RoomShift") bool RotateFace(int32 Face, int32 Direction);
    UFUNCTION(BlueprintCallable, Category="RoomShift") bool Assemble();
    UFUNCTION(BlueprintCallable, Category="RoomShift") void Undo();
    UFUNCTION(BlueprintCallable, Category="RoomShift") void ResetPuzzle();
    UFUNCTION(BlueprintCallable, Category="RoomShift") void ScramblePuzzle();
    UFUNCTION(BlueprintCallable, Category="RoomShift") void SelectFace(int32 Face);
    UFUNCTION(BlueprintCallable, CallInEditor, Category="RoomShift|Editor") void BakeEditableScene();
    UPROPERTY(EditAnywhere, Category="RoomShift|Layout") FVector FaceCenters[RoomShiftMaxFaces];
    int32 FaceCount() const { return NumFaces; }
    int32 FurnitureCount() const { return NumFurniture; }
    int32 JoinedCount() const;
    int32 CompletionSerial = 0;
    int32 Selected = 0;
    FRoomSnapshot State;
    FString Message;
    FString DeviceStatus = TEXT("DEVICE  /  localhost UDP 19872");
    int32 ReadyFurniture() const;
    bool Busy() const { return ActiveFace >= 0 || Landing >= 0; }
    int32 FaceForComponent(const UPrimitiveComponent* Component) const;
    FString Hint() const;
    bool RunSelfTest();
    static int32 GoalFor(int32 Furniture, int32 Face);
    static FLinearColor FaceColor(int32 Face);
private:
    UPROPERTY() TObjectPtr<USceneComponent> Faces[RoomShiftMaxFaces];
    UPROPERTY() TObjectPtr<USceneComponent> Assemblies[RoomShiftMaxFurniture];
    int32 NumFaces = RoomShiftMaxFaces;
    int32 NumFurniture = RoomShiftMaxFurniture;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> WoodMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> FabricMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> PlasterMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> SelectionMaterial;
    UPROPERTY() TObjectPtr<UStaticMesh> RoundedMesh;
    TArray<FRoomPiece> Pieces;
    TArray<FRoomSnapshot> History;
    TMap<const UPrimitiveComponent*, int32> SurfaceOwners;
    int32 ActiveFace = -1;
    int32 Landing = -1;
    float LandingTime = 0.f;
    float Elapsed = 0.f;
    FQuat StartQuat, EndQuat;
    FSocket* Socket = nullptr;
    TMap<FString, int64> DeviceSequences;
    FString ActiveSession;
    double LastDeviceTime = 0;
    float CaptureTimer = 0.f;
    bool CaptureRequested = false;
    bool bBakingEditableScene = false;
    FTransform FaceTransform(int32 Face, int32 Quarter) const;
    void LoadVisualAssets();
    bool BindEditableScene();
    UStaticMeshComponent* Box(FString Name, FVector Position, FVector Size, FLinearColor Color, USceneComponent* Parent=nullptr);
    void Piece(FString Name, int32 Furniture, int32 Face, FVector WorldPosition, FVector Size, FLinearColor Color);
    void BuildRoom();
    void BuildFurniture();
    void Restore(const FRoomSnapshot& Snapshot);
    void PollDevice();
    void UpdateHighlight();
};

UCLASS()
class ROOMSHIFT_API ARoomShiftController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;
    ARoomShiftPuzzle* Puzzle() const;
    void AssemblyFeedback();
    // Degrees relative to a hardware-calibrated forward direction; roll stays level.
    UFUNCTION(BlueprintCallable, Category="RoomShift|View") bool SetViewAngles(float Yaw, float Pitch);
    bool bReducedMotion = false;
private:
    FRotator ViewRotation = FRotator(12.f, 0.f, 0.f);
    bool Looking = false;
    float FeedbackRemaining = 0.f;
};

UCLASS()
class ROOMSHIFT_API ARoomShiftHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    bool HandleClick(float X, float Y);
private:
    float UIScale = 1.f;
    float DockX = 0.f, DockY = 0.f;
    bool bShowHelp = false;
    void Button(float X, float Y, float W, const FString& Label, bool Enabled=true);
};

UCLASS()
class ROOMSHIFT_API ARoomShiftMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARoomShiftMode();
    virtual void BeginPlay() override;
};
