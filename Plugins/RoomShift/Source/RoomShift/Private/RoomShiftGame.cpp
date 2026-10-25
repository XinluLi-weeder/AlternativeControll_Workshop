#include "RoomShiftGame.h"
#include "Modules/ModuleManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "DrawDebugHelpers.h"
#include "UnrealClient.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, RoomShift)
namespace
{
    constexpr float AssemblyLift = 180.f;
    const TCHAR* FurnitureNames[RoomShiftMaxFurniture] = {TEXT("SOFA"), TEXT("COFFEE TABLE"), TEXT("DOOR"), TEXT("ARMCHAIR"), TEXT("BOOKCASE"), TEXT("WRITING DESK")};
    const TCHAR* FaceNames[RoomShiftMaxFaces] = {TEXT("A / FRONT"), TEXT("B / RIGHT"), TEXT("C / LEFT"), TEXT("D / BACK"), TEXT("E / CEILING")};
    int32 Mod4(int32 Value) { return (Value % 4 + 4) % 4; }
}

ARoomShiftPuzzle::ARoomShiftPuzzle()
{
    PrimaryActorTick.bCanEverTick = true;
    FaceCenters[0]=FVector(530,0,320);
    FaceCenters[1]=FVector(60,530,320);
    FaceCenters[2]=FVector(60,-530,320);
    FaceCenters[3]=FVector(-600,0,320);
    FaceCenters[4]=FVector(-30,0,630);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
    for (int32 I=0; I<RoomShiftMaxFaces; ++I)
    {
        Faces[I] = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Face_%d"),I));
        Faces[I]->SetupAttachment(RootComponent);
    }
    for (int32 I=0; I<RoomShiftMaxFurniture; ++I)
    {
        Assemblies[I] = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Assembly_%d"),I));
        Assemblies[I]->SetupAttachment(RootComponent);
    }
}

FLinearColor ARoomShiftPuzzle::FaceColor(int32 Face)
{
    const FLinearColor Colors[] = {FLinearColor(.9f,.52f,.24f),FLinearColor(.28f,.63f,.75f),FLinearColor(.52f,.7f,.43f),FLinearColor(.68f,.45f,.72f),FLinearColor(.83f,.72f,.36f)};
    return Colors[FMath::Clamp(Face,0,RoomShiftMaxFaces-1)];
}

int32 ARoomShiftPuzzle::GoalFor(int32 Furniture, int32 Face)
{
    // -1 means this wall has no part in that assembly.
    static const int32 Goals[RoomShiftMaxFurniture][RoomShiftMaxFaces] = {
        {1,3,-1,-1,-1}, {2,-1,2,-1,-1}, {-1,1,0,-1,-1},
        {-1,-1,1,3,-1}, {3,-1,-1,1,2}, {-1,2,3,-1,1}};
    return Furniture>=0&&Furniture<RoomShiftMaxFurniture&&Face>=0&&Face<RoomShiftMaxFaces?Goals[Furniture][Face]:-1;
}

FTransform ARoomShiftPuzzle::FaceTransform(int32 Face, int32 Quarter) const
{
    const FVector Axes[] = {FVector::ForwardVector,FVector::RightVector,FVector::RightVector,FVector::ForwardVector,FVector::UpVector};
    return FTransform(FQuat(Axes[Face],FMath::DegreesToRadians(90.f*Quarter)),FaceCenters[Face]);
}

UStaticMeshComponent* ARoomShiftPuzzle::Box(FString Name, FVector Position, FVector Size, FLinearColor Color, USceneComponent* Parent)
{
    auto* Mesh = NewObject<UStaticMeshComponent>(this,*Name);
    AddInstanceComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetupAttachment(Parent ? Parent : GetRootComponent());
    Mesh->SetRelativeLocation(Position);
    Mesh->SetRelativeScale3D(Size/100.f);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Mesh->RegisterComponent();
    const bool Wood=Name.Contains(TEXT("Wood"))||Name.Contains(TEXT("Plank"))||Name.Contains(TEXT("TableTop"))||Name.Contains(TEXT("SofaFoot"))||Name.Contains(TEXT("Door"));
    const bool Fabric=Name.Contains(TEXT("SofaSeat"))||Name.Contains(TEXT("SofaBack"))||Name.Contains(TEXT("SofaArm"))||Name.Contains(TEXT("Pillow"))||Name.Contains(TEXT("Rug"));
    const bool Plaster=Name.Contains(TEXT("Shell"))||Name.Contains(TEXT("Ceiling"))||Name.Contains(TEXT("RotatingWall"));
    UMaterialInterface* Surface=Fabric?FabricMaterial.Get():Wood?WoodMaterial.Get():Plaster?PlasterMaterial.Get():BaseMaterial.Get();
    if(RoundedMesh && ((Fabric&&!Name.Contains(TEXT("Rug")))||Name.Contains(TEXT("TableTop")))) Mesh->SetStaticMesh(RoundedMesh);
    if(Surface)
    {
        auto* Material=UMaterialInstanceDynamic::Create(Surface,this);
        Material->SetVectorParameterValue(TEXT("Tint"),Color);
        Material->SetVectorParameterValue(TEXT("Color"),Color);
        if(Name.Contains(TEXT("Metal"))||Name.Contains(TEXT("Handle")))
        {
            Material->SetScalarParameterValue(TEXT("Metallic"),.75f);
            Material->SetScalarParameterValue(TEXT("Roughness"),.28f);
        }
        Mesh->SetMaterial(0,Material);
    }
    return Mesh;
}

void ARoomShiftPuzzle::Piece(FString Name,int32 Furniture,int32 Face,FVector WorldPosition,FVector Size,FLinearColor Color)
{
    WorldPosition.Z+=AssemblyLift;
    auto* Mesh=Box(Name,FVector::ZeroVector,Size,Color,Faces[Face]);
    const FTransform GoalWorld(FQuat::Identity,WorldPosition,Size/100.f);
    const FTransform Local=GoalWorld.GetRelativeTransform(FaceTransform(Face,GoalFor(Furniture,Face)));
    Mesh->SetRelativeTransform(Local);
    FRoomPiece P; P.Mesh=Mesh; P.Local=Local; P.Furniture=Furniture; P.Face=Face;
    Pieces.Add(P);
}

void ARoomShiftPuzzle::BuildRoom()
{
    Box(TEXT("Floor"),FVector(-30,0,-22),FVector(1220,1160,40),FLinearColor(.11,.075,.045));
    Box(TEXT("Ceiling"),FVector(-30,0,660),FVector(1220,1160,24),FLinearColor(.72,.73,.70));
    Box(TEXT("FrontShell"),FVector(570,0,325),FVector(35,1160,650),FLinearColor(.56,.51,.43));
    Box(TEXT("RightShell"),FVector(-30,570,325),FVector(1220,35,650),FLinearColor(.48,.55,.54));
    Box(TEXT("LeftShell"),FVector(-30,-570,325),FVector(1220,35,650),FLinearColor(.56,.60,.51));
    Box(TEXT("BackShell"),FVector(-640,0,325),FVector(35,1160,650),FLinearColor(.64,.61,.55));
    for(int32 Row=0;Row<40;++Row) for(int32 Col=0;Col<7;++Col)
    {
        const float Shade=.85f+.035f*((Row*13+Col*7)%8);
        Box(FString::Printf(TEXT("FloorPlank_%d_%d"),Row,Col),FVector(-552+Col*174, -546+Row*28, -1),FVector(173.6,27.6,2),FLinearColor(.56,.38,.21)*Shade);
    }
    Box(TEXT("FrontWoodSkirting"),FVector(544,0,9),FVector(10,1120,18),FLinearColor(.24,.16,.09));
    for(int32 Side=-1;Side<=1;Side+=2)
        Box(FString::Printf(TEXT("SideWoodSkirting%d"),Side),FVector(-30,Side*546,9),FVector(1180,10,18),FLinearColor(.24,.16,.09));
    Box(TEXT("Rug"),FVector(100,-110,1),FVector(410,340,2),FLinearColor(.20,.25,.24));
    for(int32 I=0;I<NumFaces;++I)
    {
        Faces[I]->SetRelativeTransform(FaceTransform(I,0));
        const bool Front=I==0||I==3, Ceiling=I==4;
        const float Inward=I==0||I==1||I==4?-1.f:1.f;
        auto InPlane=[Front,Ceiling,Inward](float U,float V,float Depth){ return Ceiling?FVector(U,V,Depth*Inward):Front?FVector(Depth*Inward,U,V):FVector(U,Depth*Inward,V); };
        auto* Disk=Box(FString::Printf(TEXT("RotatingWall_%d"),I),FVector::ZeroVector,FVector(600,600,14),FLinearColor(.31,.33,.31),Faces[I]);
        Disk->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        Disk->SetRelativeRotation(Ceiling?FRotator::ZeroRotator:Front?FRotator(90,0,0):FRotator(0,0,90));
        SurfaceOwners.Add(Disk,I);
        for(int32 J=0;J<4;++J)
        {
            float A=PI*.5f*J;
            FVector P=InPlane(275*FMath::Cos(A),275*FMath::Sin(A),12);
            auto* Mark=Box(FString::Printf(TEXT("WallMark_%d_%d"),I,J),P,Ceiling?FVector(24,24,8):Front?FVector(8,24,24):FVector(24,8,24),FaceColor(I),Faces[I]);
            SurfaceOwners.Add(Mark,I);
        }
        // A visible ring belongs to the selected face and is hit-testable with its disk.
        for(int32 J=0;J<64;++J)
        {
            const float A=2*PI*J/64.f;
            FVector P=InPlane(306*FMath::Cos(A),306*FMath::Sin(A),16);
            auto* Ring=Box(FString::Printf(TEXT("SelectionRing_%d_%d"),I,J),P,Ceiling?FVector(7,32,5):Front?FVector(5,7,32):FVector(7,5,32),FaceColor(I)*.3f,Faces[I]);
            Ring->SetRelativeRotation(FQuat(Ceiling?FVector::UpVector:Front?FVector::ForwardVector:FVector::RightVector,Front||Ceiling?A:-A));
            Ring->SetCastShadow(false); SurfaceOwners.Add(Ring,I);
        }
        auto* Label=NewObject<UTextRenderComponent>(this,*FString::Printf(TEXT("WallLabel%d"),I));
        AddInstanceComponent(Label); Label->SetupAttachment(RootComponent);
        Label->SetRelativeLocation(FaceTransform(I,0).GetLocation()+InPlane(0,265,28));
        Label->SetRelativeRotation(Ceiling?FRotator(-90,0,0):FRotator(0,I==0?180:I==3?0:I==1?-90:90,0));
        Label->SetText(FText::FromString(FString::Printf(TEXT("%c"),TCHAR('A'+I))));
        Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(32); Label->SetTextRenderColor(FaceColor(I).ToFColor(true)); Label->RegisterComponent();
    }
    const FVector Lights[] = {FVector(-50,0,600),FVector(-100,-350,300),FVector(-200,350,300),FVector(330,40,520)};
    for(int32 I=0;I<4;++I)
    {
        auto* Light=GetWorld()->SpawnActor<APointLight>(Lights[I],FRotator::ZeroRotator);
        Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        auto* PointLight = CastChecked<UPointLightComponent>(Light->GetLightComponent());
        PointLight->SetIntensity(I==0?21000.f:14000.f);
        PointLight->SetAttenuationRadius(1800.f);
        PointLight->SetSourceRadius(45.f);
        if(I==1||I==2) PointLight->SetCastShadows(false);
        Light->GetLightComponent()->SetLightColor(I==0?FLinearColor(1,.90,.78):FLinearColor(.86,.93,1));
#if WITH_EDITOR
        if(bBakingEditableScene)
        {
            Light->Tags.Add(TEXT("RS_EDITABLE_LIGHT"));
            Light->SetActorLabel(FString::Printf(TEXT("RoomLight_%d"),I));
            Light->SetFolderPath(TEXT("RoomShift/Lights"));
        }
#endif
    }
}

void ARoomShiftPuzzle::BuildFurniture()
{
    const FLinearColor Fabric(.34,.44,.43), Wood(.36,.19,.08), Metal(.07,.08,.085), Door(.32,.20,.10);
    // Every source transform is derived from a common assembled world pose.
    // This guarantees exact geometric alignment at the declared target states.
    Piece(TEXT("SofaBase"),0,0,FVector(280,-100,24),FVector(100,235,20),Metal);
    for(int32 I=0;I<3;++I)
    {
        Piece(FString::Printf(TEXT("SofaSeat%d"),I),0,0,FVector(270,-175+I*75,43),FVector(88,72,22),Fabric);
        Piece(FString::Printf(TEXT("SofaBack%d"),I),0,1,FVector(321,-175+I*75,70),FVector(24,72,65),Fabric*.9f);
    }
    for(int32 I=-1;I<=1;I+=2)
    {
        Piece(FString::Printf(TEXT("SofaArm%d"),I),0,1,FVector(280,-100+I*128,47),FVector(105,22,66),Fabric*.85f);
        for(int32 J=-1;J<=1;J+=2) Piece(FString::Printf(TEXT("SofaFoot%d%d"),I,J),0,0,FVector(280+J*36,-100+I*100,8),FVector(12,12,16),Wood);
    }
    Piece(TEXT("TableTop"),1,2,FVector(70,-100,44),FVector(150,85,8),Wood);
    Piece(TEXT("TableEdgeMetal"),1,2,FVector(70,-100,38),FVector(140,75,4),Metal);
    for(int32 I=-1;I<=1;I+=2) for(int32 J=-1;J<=1;J+=2)
        Piece(FString::Printf(TEXT("TableLegMetal%d%d"),I,J),1,0,FVector(70+I*62,-100+J*30,20),FVector(7,7,40),Metal);
    for(int32 I=-1;I<=1;I+=2) Piece(FString::Printf(TEXT("DoorJamb%d"),I),2,1,FVector(250+I*62,490,115),FVector(14,22,230),Wood);
    Piece(TEXT("DoorLintel"),2,1,FVector(250,490,237),FVector(138,22,14),Wood);
    Piece(TEXT("DoorLeaf"),2,2,FVector(250,488,114),FVector(108,9,224),Door);
    Piece(TEXT("DoorInsetTop"),2,2,FVector(250,481,169),FVector(82,5,86),Door*.75f);
    Piece(TEXT("DoorInsetBottom"),2,2,FVector(250,481,60),FVector(82,5,86),Door*.75f);
    Piece(TEXT("DoorHandleMetal"),2,2,FVector(209,473,112),FVector(5,9,20),FLinearColor(.65,.48,.20));
    // Primitive fallback; the saved Studio level replaces these with sliced imported furniture.
    for(int32 F=3;F<RoomShiftMaxFurniture;++F)
        for(int32 I=0;I<RoomShiftMaxFaces;++I) if(GoalFor(F,I)>=0)
            Piece(FString::Printf(TEXT("Fallback_%d_%d"),F,I),F,I,FVector(-200+F*20,-280+I*40,40),FVector(30,30,80),Wood);
}

void ARoomShiftPuzzle::LoadVisualAssets()
{
    BaseMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Surface.M_Surface"));
    if(!BaseMaterial) BaseMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    WoodMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Wood.M_Wood"));
    FabricMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Fabric.M_Fabric"));
    PlasterMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Plaster.M_Plaster"));
    RoundedMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/RoomShift/SM_RoundedBox.SM_RoundedBox"));
    SelectionMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/Studio/M_Selection.M_Selection"));
    if(!WoodMaterial)WoodMaterial=BaseMaterial; if(!FabricMaterial)FabricMaterial=BaseMaterial; if(!PlasterMaterial)PlasterMaterial=BaseMaterial;
}

bool ARoomShiftPuzzle::BindEditableScene()
{
    int32 Found=0;
    int32 MaxFace=-1, MaxFurniture=-1;
    for(int32 I=0;I<RoomShiftMaxFaces;++I) Faces[I]->SetRelativeTransform(FaceTransform(I,0));
    for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
    {
        AStaticMeshActor* Actor=*It;
        if(!Actor->ActorHasTag(TEXT("RS_AUTHORED"))) continue;
        ++Found;
        UStaticMeshComponent* Mesh=Actor->GetStaticMeshComponent();
        if(!Mesh) continue;
        Mesh->SetMobility(EComponentMobility::Movable);
        int32 Face=-1,Furniture=-1,Select=-1;
        for(const FName& Tag:Actor->Tags)
        {
            const FString Value=Tag.ToString();
            if(Value.StartsWith(TEXT("RS_FACE_"))) Face=FCString::Atoi(*Value.RightChop(8));
            else if(Value.StartsWith(TEXT("RS_FURNITURE_"))) Furniture=FCString::Atoi(*Value.RightChop(13));
            else if(Value.StartsWith(TEXT("RS_SELECT_"))) Select=FCString::Atoi(*Value.RightChop(10));
        }
        if(Actor->ActorHasTag(TEXT("RS_RING"))) Mesh->ComponentTags.AddUnique(TEXT("RS_RING"));
        if(Face>=0&&Face<RoomShiftMaxFaces)
        {
            MaxFace=FMath::Max(MaxFace,Face);
            Actor->AttachToComponent(Faces[Face],FAttachmentTransformRules::KeepWorldTransform);
            if(Furniture>=0&&Furniture<RoomShiftMaxFurniture)
            {
                MaxFurniture=FMath::Max(MaxFurniture,Furniture);
                FRoomPiece P; P.Mesh=Mesh; P.Local=Mesh->GetRelativeTransform(); P.Face=Face; P.Furniture=Furniture;
                Pieces.Add(P);
            }
        }
        if(Select>=0&&Select<RoomShiftMaxFaces) SurfaceOwners.Add(Mesh,Select);
        if(Furniture>=0 || Select>=0)
        {
            if(UMaterialInterface* Material=Mesh->GetMaterial(0))
                Mesh->SetMaterial(0,UMaterialInstanceDynamic::Create(Material,this));
        }
    }
    if(Found>0) { NumFaces=MaxFace+1; NumFurniture=MaxFurniture+1; }
    UE_LOG(LogTemp,Display,TEXT("ROOMSHIFT_EDITABLE_SCENE actors=%d pieces=%d surfaces=%d"),Found,Pieces.Num(),SurfaceOwners.Num());
    return Found>0;
}

void ARoomShiftPuzzle::BakeEditableScene()
{
#if WITH_EDITOR
    if(!GetWorld() || GetWorld()->WorldType!=EWorldType::Editor) return;
    for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
        if(It->ActorHasTag(TEXT("RS_AUTHORED"))) { UE_LOG(LogTemp,Warning,TEXT("Editable scene already exists; refusing to overwrite edits.")); return; }
    LoadVisualAssets(); bBakingEditableScene=true;
    BuildRoom(); BuildFurniture();
    TArray<UStaticMeshComponent*> SourceMeshes;
    GetComponents<UStaticMeshComponent>(SourceMeshes);
    for(UStaticMeshComponent* Source:SourceMeshes)
    {
        auto* Part=GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),Source->GetComponentTransform());
        if(!Part) continue;
        UStaticMeshComponent* Target=Part->GetStaticMeshComponent();
        Target->SetMobility(EComponentMobility::Movable);
        Target->SetStaticMesh(Source->GetStaticMesh());
        Target->SetWorldTransform(Source->GetComponentTransform());
        Target->SetMaterial(0,Source->GetMaterial(0));
        Target->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Target->SetCollisionResponseToAllChannels(ECR_Ignore);
        Target->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Part->Tags.Add(TEXT("RS_AUTHORED"));
        if(Source->GetName().Contains(TEXT("SelectionRing"))) Part->Tags.Add(TEXT("RS_RING"));
        int32 Face=-1,Furniture=-1;
        for(const FRoomPiece& P:Pieces) if(P.Mesh==Source) { Face=P.Face; Furniture=P.Furniture; break; }
        if(Face<0) if(const int32* SurfaceFace=SurfaceOwners.Find(Source)) Face=*SurfaceFace;
        if(Face>=0) Part->Tags.Add(FName(*FString::Printf(TEXT("RS_FACE_%d"),Face)));
        if(Furniture>=0) Part->Tags.Add(FName(*FString::Printf(TEXT("RS_FURNITURE_%d"),Furniture)));
        if(const int32* SurfaceFace=SurfaceOwners.Find(Source)) Part->Tags.Add(FName(*FString::Printf(TEXT("RS_SELECT_%d"),*SurfaceFace)));
        Part->SetActorLabel(Source->GetName());
        if(Furniture>=0) Part->SetFolderPath(FName(*FString::Printf(TEXT("RoomShift/Furniture/%s"),FurnitureNames[Furniture])));
        else if(Face>=0) Part->SetFolderPath(FName(*FString::Printf(TEXT("RoomShift/Rotating Faces/%c"),TCHAR('A'+Face))));
        else Part->SetFolderPath(Source->GetName().Contains(TEXT("FloorPlank"))?TEXT("RoomShift/Floor Planks"):TEXT("RoomShift/Room"));
    }
    TArray<UTextRenderComponent*> SourceLabels;
    GetComponents<UTextRenderComponent>(SourceLabels);
    for(UTextRenderComponent* Source:SourceLabels)
    {
        auto* Label=GetWorld()->SpawnActor<ATextRenderActor>(ATextRenderActor::StaticClass(),Source->GetComponentTransform());
        if(!Label) continue;
        Label->GetTextRender()->SetText(Source->Text);
        Label->GetTextRender()->SetWorldSize(Source->WorldSize);
        Label->GetTextRender()->SetTextRenderColor(Source->TextRenderColor);
        Label->GetTextRender()->SetHorizontalAlignment(Source->HorizontalAlignment);
        Label->SetActorLabel(Source->GetName());
        Label->SetFolderPath(TEXT("RoomShift/Labels"));
    }
    for(UStaticMeshComponent* Source:SourceMeshes) Source->DestroyComponent();
    for(UTextRenderComponent* Source:SourceLabels) Source->DestroyComponent();
    Pieces.Empty(); SurfaceOwners.Empty(); bBakingEditableScene=false;
    UE_LOG(LogTemp,Display,TEXT("ROOMSHIFT_EDITABLE_SCENE baked %d meshes"),SourceMeshes.Num());
#endif
}

void ARoomShiftPuzzle::BeginPlay()
{
    Super::BeginPlay();
    LoadVisualAssets();
    if(!BindEditableScene()) { BuildRoom(); BuildFurniture(); }
    UpdateHighlight();
    if(FParse::Param(FCommandLine::Get(),TEXT("RoomShiftShowSolved")))
    {
        FRoomSnapshot Solved;
        for(int32 F=0;F<NumFurniture;++F) Solved.Joined[F]=true;
        Restore(Solved);
    }
    Message=JoinedCount()==NumFurniture?TEXT("ROOM RESTORED / Explore your completed room."):TEXT("Restore the sofa first: A 90 / B 270. Select a face, turn, then ASSEMBLE.");
    Socket=FUdpSocketBuilder(TEXT("RoomShiftInput")).AsNonBlocking().BoundToAddress(FIPv4Address(127,0,0,1)).BoundToPort(19872).WithReceiveBufferSize(65536);
    if(!Socket) DeviceStatus=TEXT("DEVICE  /  port 19872 unavailable (mouse still works)");
    if(FParse::Param(FCommandLine::Get(),TEXT("RoomShiftSelfTest")))
    {
        const bool Passed=RunSelfTest();
        UE_LOG(LogTemp,Display,TEXT("ROOMSHIFT_SELF_TEST %s"),Passed?TEXT("PASS"):TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
    }
}

bool ARoomShiftPuzzle::RotateFace(int32 Face,int32 Direction)
{
    if(Busy() || Face<0 || Face>=NumFaces || (Direction!=1 && Direction!=-1)) return false;
    History.Add(State); Selected=Face; ActiveFace=Face; Elapsed=0;
    UpdateHighlight();
    StartQuat=Faces[Face]->GetRelativeRotation().Quaternion();
    State.Turns[Face]=Mod4(State.Turns[Face]+Direction);
    EndQuat=FaceTransform(Face,State.Turns[Face]).GetRotation();
    ++State.Moves;
    Message=FString::Printf(TEXT("Turning %s..."),FaceNames[Face]);
    return true;
}

void ARoomShiftPuzzle::Tick(float Delta)
{
    Super::Tick(Delta);
    FString CapturePath;
    if(FParse::Value(FCommandLine::Get(),TEXT("RoomShiftCapture="),CapturePath))
    {
        CaptureTimer+=Delta;
        if(CaptureTimer>8.f&&!CaptureRequested)
        {
            FScreenshotRequest::RequestScreenshot(CapturePath,true,false);
            CaptureRequested=true;
        }
        if(CaptureTimer>12.f) FPlatformMisc::RequestExit(false);
    }
    if(ActiveFace>=0)
    {
        Elapsed+=Delta;
        const float T=FMath::Clamp(Elapsed/.55f,0.f,1.f), Smooth=T*T*(3-2*T);
        Faces[ActiveFace]->SetRelativeRotation(FQuat::Slerp(StartQuat,EndQuat,Smooth));
        if(T>=1)
        {
            Faces[ActiveFace]->SetRelativeTransform(FaceTransform(ActiveFace,State.Turns[ActiveFace]));
            ActiveFace=-1;
            const int32 Ready=ReadyFurniture();
            Message=Ready>=0?FString::Printf(TEXT("%s aligned. Click ASSEMBLE to lock the parts together."),FurnitureNames[Ready]):TEXT("Inspect the fragments, or click HINT for a target orientation.");
        }
    }
    if(Landing>=0)
    {
        LandingTime+=Delta;
        const float T=FMath::Clamp(LandingTime/1.2f,0.f,1.f);
        Assemblies[Landing]->SetRelativeLocation(FVector(0,0,-AssemblyLift*T*T*(3-2*T)));
        if(T>=1)
        {
            Landing=-1;
            Message=JoinedCount()==NumFurniture?TEXT("ROOM RESTORED / All six objects are on the floor. RMB drag to look around."):TEXT("Furniture restored. Try the next object, or use HINT for its target angles.");
        }
    }
    PollDevice();
    // Fine colored tethers explain which wall carries each floating assembly group.
    for(int32 F=0;F<NumFurniture;++F)
    {
        FVector Center=FVector::ZeroVector; int32 Count=0;
        for(const auto& P:Pieces)
            if(P.Furniture==F && !State.Joined[F] && P.Face==Selected) { Center+=P.Mesh->GetComponentLocation(); ++Count; }
        if(Count)
        {
            Center/=Count; FVector Mount=Center;
            if(Selected==0||Selected==3) Mount.X=FaceCenters[Selected].X;
            else if(Selected==4) Mount.Z=FaceCenters[Selected].Z;
            else Mount.Y=FaceCenters[Selected].Y;
            DrawDebugLine(GetWorld(),Center,Mount,FaceColor(Selected).ToFColor(true),false,-1,0,.7f);
        }
    }
}

int32 ARoomShiftPuzzle::ReadyFurniture() const
{
    if(Busy()) return -1;
    for(int32 F=0;F<NumFurniture;++F)
    {
        if(State.Joined[F]) continue;
        bool Match=true;
        bool HasPieces=false;
        for(const auto& P:Pieces) if(P.Furniture==F) HasPieces=true;
        for(int32 I=0;I<NumFaces;++I) if(GoalFor(F,I)>=0 && GoalFor(F,I)!=State.Turns[I]) Match=false;
        Match &= HasPieces;
        if(Match) return F;
    }
    return -1;
}

bool ARoomShiftPuzzle::Assemble()
{
    int32 F=ReadyFurniture(); if(F<0) return false;
    History.Add(State); State.Joined[F]=true;
    for(auto& P:Pieces) if(P.Furniture==F)
        P.Mesh->AttachToComponent(Assemblies[F],FAttachmentTransformRules::KeepWorldTransform);
    Landing=F; LandingTime=0.f; UpdateHighlight();
    ++CompletionSerial;
    if(auto* PC=Cast<ARoomShiftController>(GetWorld()->GetFirstPlayerController())) PC->AssemblyFeedback();
    Message=FString::Printf(TEXT("%s complete! Placing it on the floor..."),FurnitureNames[F]);
    return true;
}

void ARoomShiftPuzzle::Restore(const FRoomSnapshot& Snapshot)
{
    ActiveFace=-1; Landing=-1; State=Snapshot;
    for(int32 I=0;I<NumFaces;++I) Faces[I]->SetRelativeTransform(FaceTransform(I,State.Turns[I]));
    for(int32 I=0;I<NumFurniture;++I) Assemblies[I]->SetRelativeLocation(State.Joined[I]?FVector(0,0,-AssemblyLift):FVector::ZeroVector);
    for(auto& P:Pieces)
    {
        P.Mesh->AttachToComponent(Faces[P.Face],FAttachmentTransformRules::KeepRelativeTransform);
        P.Mesh->SetRelativeTransform(P.Local);
        if(State.Joined[P.Furniture])
        {
            const FTransform SolvedWorld=P.Local*FaceTransform(P.Face,GoalFor(P.Furniture,P.Face));
            P.Mesh->AttachToComponent(Assemblies[P.Furniture],FAttachmentTransformRules::KeepRelativeTransform);
            P.Mesh->SetRelativeTransform(SolvedWorld);
        }
    }
    UpdateHighlight();
}

void ARoomShiftPuzzle::Undo()
{
    if(Busy()||History.IsEmpty()) return;
    const auto Snapshot=History.Pop(); Restore(Snapshot); Message=TEXT("Undone. Object ownership and wall orientations restored.");
}
void ARoomShiftPuzzle::ResetPuzzle()
{
    if(Busy()) return;
    History.Add(State); Restore(FRoomSnapshot()); Message=TEXT("Room reset. All furniture fragments restored.");
}
void ARoomShiftPuzzle::SelectFace(int32 Face)
{
    if(Face<0||Face>=NumFaces)return;
    Selected=Face; UpdateHighlight();
    if(!Busy())Message=FString::Printf(TEXT("Selected %s / The glowing wall and highlighted fragments rotate together."),FaceNames[Face]);
}

void ARoomShiftPuzzle::ScramblePuzzle()
{
    if(Busy())return;
    History.Add(State);
    FRoomSnapshot Mix;
    for(int32 Attempt=0;Attempt<32;++Attempt)
    {
        for(int32 I=0;I<NumFaces;++I) Mix.Turns[I]=FMath::RandRange(0,3);
        Restore(Mix);
        if(ReadyFurniture()<0)break;
    }
    if(ReadyFurniture()>=0)Restore(FRoomSnapshot());
    Message=TEXT("NEW MIX / A fresh starting arrangement. Restore all six objects in fewer turns.");
}

void ARoomShiftPuzzle::UpdateHighlight()
{
    for(const auto& Entry:SurfaceOwners)
    {
        auto* Mesh=Cast<UStaticMeshComponent>(const_cast<UPrimitiveComponent*>(Entry.Key));
        auto* Material=Mesh?Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)):nullptr;
        if(!Material)continue;
        const bool SelectedWall=Entry.Value==Selected;
        const bool Ring=Mesh->ComponentHasTag(TEXT("RS_RING"))||Mesh->GetName().Contains(TEXT("SelectionRing"));
        Material->SetVectorParameterValue(TEXT("HighlightColor"),FaceColor(Entry.Value));
        Material->SetScalarParameterValue(TEXT("Highlight"),SelectedWall?(Ring?1.f:.035f):(Ring?.008f:0.f));
        if(!Ring) Material->SetVectorParameterValue(TEXT("Tint"),SelectedWall?FLinearColor(.31,.33,.31)+FaceColor(Entry.Value)*.18f:FLinearColor(.31,.33,.31));
    }
    for(const auto& P:Pieces)
    {
        P.Mesh->SetOverlayMaterial(!State.Joined[P.Furniture]&&P.Face==Selected?SelectionMaterial.Get():nullptr);
        if(auto* Material=Cast<UMaterialInstanceDynamic>(P.Mesh->GetMaterial(0)))
        {
            Material->SetVectorParameterValue(TEXT("HighlightColor"),FaceColor(P.Face));
            Material->SetScalarParameterValue(TEXT("Highlight"),!State.Joined[P.Furniture]&&P.Face==Selected?.12f:0.f);
        }
    }
}

int32 ARoomShiftPuzzle::FaceForComponent(const UPrimitiveComponent* Component) const
{
    if(const int32* I=SurfaceOwners.Find(Component)) return *I;
    for(const auto& P:Pieces) if(P.Mesh==Component) return State.Joined[P.Furniture]?-1:P.Face;
    return -1;
}

FString ARoomShiftPuzzle::Hint() const
{
    for(int32 F=0;F<NumFurniture;++F) if(!State.Joined[F])
    {
        FString Result=FString::Printf(TEXT("%s / "),FurnitureNames[F]);
        for(int32 I=0;I<NumFaces;++I) if(GoalFor(F,I)>=0) Result+=FString::Printf(TEXT("%c = %d deg   "),TCHAR('A'+I),GoalFor(F,I)*90);
        return Result+TEXT("then ASSEMBLE.");
    }
    return TEXT("All restored. Completed furniture stays on the floor. RMB drag to look around.");
}

int32 ARoomShiftPuzzle::JoinedCount() const
{
    int32 N=0; for(int32 F=0;F<NumFurniture;++F) N+=State.Joined[F]?1:0; return N;
}

void ARoomShiftPuzzle::PollDevice()
{
    if(!Socket) return;
    uint32 Size=0;
    for(int32 N=0;N<16&&Socket->HasPendingData(Size);++N)
    {
        uint8 Buffer[1025]; int32 Read=0;
        TSharedRef<FInternetAddr> Sender=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if(!Socket->RecvFrom(Buffer,1024,Read,*Sender)||Read<=0) continue;
        Buffer[Read]=0;
        TSharedPtr<FJsonObject> Obj;
        auto Reader=TJsonReaderFactory<>::Create(FString(UTF8_TO_TCHAR(reinterpret_cast<char*>(Buffer))));
        if(!FJsonSerializer::Deserialize(Reader,Obj)||!Obj.IsValid()) continue;
        FString Session,Cmd; double Sequence;
        if(!Obj->TryGetStringField(TEXT("session"),Session)||Session.IsEmpty()||Session.Len()>64||!Obj->TryGetStringField(TEXT("cmd"),Cmd)||!Obj->TryGetNumberField(TEXT("seq"),Sequence)||!FMath::IsFinite(Sequence)||Sequence<0||Sequence>2147483647||Sequence!=FMath::FloorToDouble(Sequence)) continue;
        FString Status=TEXT("invalid"); int64 Seq=static_cast<int64>(Sequence);
        if(Cmd==TEXT("status")) Status=TEXT("ok"); // Heartbeat/feedback is available during animations too.
        else if(const int64* Last=DeviceSequences.Find(Session);Last&&Seq<=*Last) Status=TEXT("duplicate");
        else if(Busy()&&Cmd!=TEXT("view")) Status=TEXT("busy");
        else
        {
            bool Accepted=false;
            if(Cmd==TEXT("view"))
            {
                double Yaw,Pitch;
                if(Obj->TryGetNumberField(TEXT("yaw"),Yaw)&&Obj->TryGetNumberField(TEXT("pitch"),Pitch))
                    if(auto* PC=Cast<ARoomShiftController>(GetWorld()->GetFirstPlayerController()))
                        Accepted=PC->SetViewAngles(static_cast<float>(Yaw),static_cast<float>(Pitch));
            }
            else if(Cmd==TEXT("rotate"))
            {
                double Face,Dir;
                if(Obj->TryGetNumberField(TEXT("face"),Face)&&Obj->TryGetNumberField(TEXT("dir"),Dir)&&Face>=0&&Face<NumFaces&&Face==FMath::FloorToDouble(Face)&&(Dir==1||Dir==-1)) Accepted=RotateFace(static_cast<int32>(Face),static_cast<int32>(Dir));
            }
            else if(Cmd==TEXT("assemble")) Accepted=Assemble();
            else if(Cmd==TEXT("undo")) { Undo(); Accepted=true; }
            else if(Cmd==TEXT("reset")) { ResetPuzzle(); Accepted=true; }
            if(Accepted)
            {
                if(DeviceSequences.Num()>32) DeviceSequences.Empty();
                DeviceSequences.Add(Session,Seq); Status=TEXT("ok");
                DeviceStatus=TEXT("DEVICE CONNECTED");
            }
        }
        auto ReplyObject=MakeShared<FJsonObject>();
        ReplyObject->SetNumberField(TEXT("seq"),Seq); ReplyObject->SetStringField(TEXT("status"),Status);
        ReplyObject->SetNumberField(TEXT("completion_serial"),CompletionSerial);
        ReplyObject->SetBoolField(TEXT("busy"),Busy());
        TArray<TSharedPtr<FJsonValue>> Turns,Joined;
        for(int32 I=0;I<NumFaces;++I) Turns.Add(MakeShared<FJsonValueNumber>(State.Turns[I]));
        for(int32 F=0;F<NumFurniture;++F) Joined.Add(MakeShared<FJsonValueBoolean>(State.Joined[F]));
        ReplyObject->SetArrayField(TEXT("turns"),Turns); ReplyObject->SetArrayField(TEXT("joined"),Joined);
        FString Reply; auto Writer=TJsonWriterFactory<>::Create(&Reply); FJsonSerializer::Serialize(ReplyObject,Writer);
        FTCHARToUTF8 Data(*Reply); int32 Sent=0; Socket->SendTo(reinterpret_cast<const uint8*>(Data.Get()),Data.Length(),Sent,*Sender);
    }
}

void ARoomShiftPuzzle::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket=nullptr; }
    Super::EndPlay(Reason);
}

bool ARoomShiftPuzzle::RunSelfTest()
{
    bool OK=true;
    auto Check=[&](bool Condition,const TCHAR* Name){ UE_LOG(LogTemp,Display,TEXT("ROOMSHIFT_CHECK %s %s"),Name,Condition?TEXT("PASS"):TEXT("FAIL")); OK &= Condition; };
    Check(NumFaces==5&&NumFurniture==6,TEXT("five-faces-six-assemblies"));
    Check(!RotateFace(-1,1)&&!RotateFace(5,1)&&!RotateFace(0,0),TEXT("reject-invalid-command"));
    for(int32 Face=0;Face<NumFaces;++Face)
    {
        const FTransform Initial=Faces[Face]->GetComponentTransform();
        for(int32 I=0;I<4;++I)
        {
            Check(RotateFace(Face,1),TEXT("quarter-turn"));
            for(int32 Sample=0;Sample<4;++Sample)
            {
                Tick(.14f);
                for(const auto& P:Pieces)
                {
                    const auto B=P.Mesh->Bounds;
                    Check(B.Origin.Z-B.BoxExtent.Z>=-1.f&&B.Origin.Z+B.BoxExtent.Z<=620.f,TEXT("rotating-fragments-inside-floor-ceiling"));
                }
            }
        }
        Check(Faces[Face]->GetComponentTransform().Equals(Initial,.001f),TEXT("full-circle-each-axis"));
    }
    Restore(FRoomSnapshot()); History.Empty();
    Check(!Assemble(),TEXT("unaligned-cannot-assemble"));
    for(int32 F=0;F<NumFurniture;++F)
    {
        FRoomSnapshot S=State;
        for(int32 Face=0;Face<NumFaces;++Face) if(GoalFor(F,Face)>=0) S.Turns[Face]=GoalFor(F,Face);
        Restore(S);
        int32 PartCount=0; TSet<int32> Donors;
        for(const auto& P:Pieces) if(P.Furniture==F) { ++PartCount; Donors.Add(P.Face); }
        Check(PartCount>=2&&Donors.Num()>=2,TEXT("furniture-has-multiple-donors"));
        TArray<FTransform> Before; for(const auto& P:Pieces) Before.Add(P.Mesh->GetComponentTransform());
        const int32 SerialBefore=CompletionSerial;
        Check(ReadyFurniture()==F&&Assemble(),TEXT("assemble-target"));
        Check(CompletionSerial==SerialBefore+1,TEXT("one-feedback-per-assembly"));
        Check(!Assemble()&&CompletionSerial==SerialBefore+1,TEXT("no-duplicate-feedback"));
        for(int32 I=0;I<Pieces.Num();++I) Check(Pieces[I].Mesh->GetComponentTransform().Equals(Before[I],.01f),TEXT("assembly-preserves-pose"));
        Check(!RotateFace(0,1),TEXT("reject-during-landing")); Tick(2.f);
        float Lowest=MAX_flt;
        for(const auto& P:Pieces) if(P.Furniture==F)
        {
            Lowest=FMath::Min(Lowest,float(P.Mesh->Bounds.Origin.Z-P.Mesh->Bounds.BoxExtent.Z));
            Check(P.Mesh->GetAttachParent()==Assemblies[F],TEXT("assembled-floor-parent"));
        }
        Check(FMath::Abs(Lowest)<1.f,TEXT("furniture-touches-floor"));
        Before.Empty(); for(const auto& P:Pieces) Before.Add(P.Mesh->GetComponentTransform());
        for(int32 Face=0;Face<NumFaces;++Face) { RotateFace(Face,1); Tick(1.f); }
        for(int32 I=0;I<Pieces.Num();++I) if(State.Joined[Pieces[I].Furniture]) Check(Pieces[I].Mesh->GetComponentTransform().Equals(Before[I],.01f),TEXT("restored-furniture-stays-on-floor"));
        for(int32 Face=0;Face<NumFaces;++Face) Undo();
        Undo(); Check(!State.Joined[F],TEXT("undo-assembly"));
        for(const auto& P:Pieces) if(P.Furniture==F) Check(P.Mesh->GetAttachParent()==Faces[P.Face],TEXT("undo-restores-donor"));
        Check(Assemble(),TEXT("reassemble-after-undo")); Tick(2.f);
    }
    Check(JoinedCount()==NumFurniture,TEXT("complete-six-objects"));
    ScramblePuzzle(); Check(JoinedCount()==0&&ReadyFurniture()<0,TEXT("new-mix-restarts-unsolved"));
    Undo(); Check(JoinedCount()==NumFurniture,TEXT("undo-new-mix-restores-room"));
    ResetPuzzle(); Check(JoinedCount()==0,TEXT("reset-progress"));
    for(const auto& P:Pieces) Check(P.Mesh->GetAttachParent()==Faces[P.Face]&&P.Mesh->GetRelativeTransform().Equals(P.Local,.01f),TEXT("reset-pose-and-parent"));
    for(int32 Face=0;Face<NumFaces;++Face)
    {
        SelectFace(Face);
        for(const auto& Entry:SurfaceOwners) if(Entry.Key->ComponentHasTag(TEXT("RS_RING")))
        {
            auto* Material=Cast<UMaterialInstanceDynamic>(CastChecked<UStaticMeshComponent>(Entry.Key)->GetMaterial(0));
            Check(Material&&FMath::IsNearlyEqual(Material->K2_GetScalarParameterValue(TEXT("Highlight")),Entry.Value==Face?1.f:.008f),TEXT("selection-ring"));
        }
    }
    Restore(FRoomSnapshot()); History.Empty(); return OK;
}

ARoomShiftPuzzle* ARoomShiftController::Puzzle() const
{
    for(TActorIterator<ARoomShiftPuzzle> It(GetWorld());It;++It) return *It;
    return nullptr;
}
void ARoomShiftController::BeginPlay()
{
    Super::BeginPlay(); bShowMouseCursor=true;
    FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(Mode);
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>(FVector(-80,0,150),ViewRotation);
    Camera->GetCameraComponent()->FieldOfView=120;
    Camera->GetCameraComponent()->bConstrainAspectRatio=false;
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureMethod=true;
    Camera->GetCameraComponent()->PostProcessSettings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureBias=true;
    Camera->GetCameraComponent()->PostProcessSettings.AutoExposureBias=10.f;
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_AmbientOcclusionIntensity=true;
    Camera->GetCameraComponent()->PostProcessSettings.AmbientOcclusionIntensity=.8f;
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_AmbientOcclusionRadius=true;
    Camera->GetCameraComponent()->PostProcessSettings.AmbientOcclusionRadius=70.f;
    SetViewTarget(Camera);
}
bool ARoomShiftController::SetViewAngles(float Yaw, float Pitch)
{
    if(!FMath::IsFinite(Yaw)||!FMath::IsFinite(Pitch)) return false;
    ViewRotation=FRotator(FMath::Clamp(Pitch,-80.f,80.f),FRotator::NormalizeAxis(Yaw),0.f);
    if(GetViewTarget()) GetViewTarget()->SetActorRotation(ViewRotation);
    return true;
}

void ARoomShiftController::AssemblyFeedback()
{
    FeedbackRemaining=.28f;
    bForceFeedbackEnabled=true;
    PlayDynamicForceFeedback(.4f,.22f,true,true,true,true,EDynamicForceFeedbackAction::Start);
}

void ARoomShiftController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime); auto* P=Puzzle(); if(!P)return;
    const bool Right=IsInputKeyDown(EKeys::RightMouseButton);
    const bool Navigating=Right;
    if(Navigating!=Looking)
    {
        Looking=Navigating; bShowMouseCursor=!Navigating;
        if(Navigating)
        {
            FInputModeGameOnly Mode; Mode.SetConsumeCaptureMouseDown(false); SetInputMode(Mode);
            GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
        }
        else
        {
            FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(Mode);
            // Forward the first left click after camera navigation to the HUD as well.
            GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
        }
    }
    if(Right)
    {
        float X,Y; GetInputMouseDelta(X,Y);
        SetViewAngles(ViewRotation.Yaw+X*.24f,ViewRotation.Pitch+Y*.24f);
    }
    auto* Camera=Cast<ACameraActor>(GetViewTarget());
    if(Camera)
    {
        FeedbackRemaining=FMath::Max(0.f,FeedbackRemaining-DeltaTime);
        const float T=.28f-FeedbackRemaining;
        const float Strength=bReducedMotion?0.f:FeedbackRemaining/.28f;
        Camera->GetCameraComponent()->SetRelativeLocation(FVector(0,FMath::Sin(T*95.f)*.9f*Strength,FMath::Sin(T*73.f)*.6f*Strength));
        // Fixed observation point: input can turn the view, never translate it.
        Camera->SetActorLocation(FVector(-80,0,150));
        if(WasInputKeyJustPressed(EKeys::Home)) SetViewAngles(0.f,12.f);
    }

    if(!Navigating&&WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        float X,Y; GetMousePosition(X,Y);
        auto* H=Cast<ARoomShiftHUD>(GetHUD());
        if(!H||!H->HandleClick(X,Y))
        {
            FHitResult Hit;
            if(GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility),false,Hit)) P->SelectFace(P->FaceForComponent(Hit.GetComponent()));
        }
    }
    if(WasInputKeyJustPressed(EKeys::One))P->SelectFace(0);
    if(WasInputKeyJustPressed(EKeys::Two))P->SelectFace(1);
    if(WasInputKeyJustPressed(EKeys::Three))P->SelectFace(2);
    if(WasInputKeyJustPressed(EKeys::Four))P->SelectFace(3);
    if(WasInputKeyJustPressed(EKeys::Five))P->SelectFace(4);
    if(WasInputKeyJustPressed(EKeys::V))bReducedMotion=!bReducedMotion;
    if(WasInputKeyJustPressed(EKeys::Q))P->RotateFace(P->Selected,-1);
    if(WasInputKeyJustPressed(EKeys::E))P->RotateFace(P->Selected,1);
    if(!Navigating&&WasInputKeyJustPressed(EKeys::SpaceBar))P->Assemble();
    if(WasInputKeyJustPressed(EKeys::Z))P->Undo();
    if(WasInputKeyJustPressed(EKeys::R))P->ResetPuzzle();
    if(WasInputKeyJustPressed(EKeys::N))P->ScramblePuzzle();
    if(WasInputKeyJustPressed(EKeys::H))P->Message=P->Hint();
}

void ARoomShiftHUD::Button(float X,float Y,float W,const FString& Label,bool Enabled)
{
    const float Scale=UIScale;
    X*=Scale; Y*=Scale; W*=Scale;
    DrawRect(Enabled?FLinearColor(.13,.2,.27,.96):FLinearColor(.08,.1,.12,.8),X,Y,W,36*Scale);
    DrawText(Label,Enabled?FLinearColor(.9,.94,1):FLinearColor(.35,.4,.45),X+12*Scale,Y+8*Scale,GEngine->GetMediumFont(),.8f*Scale);
}
void ARoomShiftHUD::DrawHUD()
{
    Super::DrawHUD(); if(!Canvas)return;
    auto* PC=Cast<ARoomShiftController>(PlayerOwner); auto* P=PC?PC->Puzzle():nullptr; if(!P)return;
    UIScale=FMath::Min(Canvas->SizeX/1280.f,Canvas->SizeY/720.f);
    const float S=UIScale, W=Canvas->SizeX/S, H=Canvas->SizeY/S;
    DockX=(W-1100.f)*.5f; DockY=H-126.f;
    auto Rect=[this,S](FLinearColor C,float X,float Y,float Width,float Height){ AHUD::DrawRect(C,X*S,Y*S,Width*S,Height*S); };
    auto Text=[this,S](const FString& T,FLinearColor C,float X,float Y,float Size=1.f){ AHUD::DrawText(T,C,X*S,Y*S,GEngine->GetSmallFont(),Size*S); };
    const FLinearColor Ink(.90,.88,.82), Muted(.57,.61,.59), Panel(.028,.034,.032,.94);
    Text(TEXT("ROOM / SHIFT"),Ink,24,18,1.3f);
    Text(FString::Printf(TEXT("%d / %d RESTORED    |    %d TURNS"),P->JoinedCount(),P->FurnitureCount(),P->State.Moves),Muted,24,43);
    // Compact status strip; no tall panel over the room.
    for(int32 F=0;F<P->FurnitureCount();++F)
    {
        const float X=W-716+F*116;
        Rect(Panel,X,16,110,28);
        Rect(P->State.Joined[F]?FLinearColor(.42,.7,.48):FLinearColor(.21,.24,.23),X,43,110,2);
        Text(FurnitureNames[F],P->State.Joined[F]?FLinearColor(.6,.85,.62):Muted,X+7,23,.85f);
    }
    Rect(Panel,DockX,DockY,1100,110);
    Text(P->Message,Ink,DockX+16,DockY+10,.95f);
    for(int32 I=0;I<P->FaceCount();++I)
    {
        float X=DockX+14+I*112;
        Rect(I==P->Selected?FLinearColor(.17,.20,.18,.98):FLinearColor(.07,.085,.075,.95),X,DockY+36,104,40);
        Rect(ARoomShiftPuzzle::FaceColor(I),X,DockY+36,3,40);
        Text(FString::Printf(TEXT("%c   %03d deg"),TCHAR('A'+I),P->State.Turns[I]*90),Ink,X+12,DockY+42,1.05f);
        Text(I==4?TEXT("CEILING"):I==3?TEXT("BACK"):I==2?TEXT("LEFT"):I==1?TEXT("RIGHT"):TEXT("FRONT"),Muted,X+12,DockY+58,.75f);
    }
    Button(DockX+580,DockY+38,82,TEXT("Q  -90"),!P->Busy());
    Button(DockX+668,DockY+38,82,TEXT("E  +90"),!P->Busy());
    Button(DockX+762,DockY+38,168,TEXT("ASSEMBLE [SPACE]"),P->ReadyFurniture()>=0);
    Button(DockX+944,DockY+38,64,TEXT("HINT"));
    Button(DockX+1014,DockY+38,70,TEXT("HELP"));
    Text(TEXT("1-5 select   |   RMB drag: look around   |   Fixed position   |   HOME: recenter"),Muted,DockX+16,DockY+88,.85f);
    Text(TEXT("UNDO [Z]"),Ink,DockX+740,DockY+88,.9f);
    Text(TEXT("NEW MIX [N]"),Ink,DockX+635,DockY+88,.9f);
    Text(TEXT("RESET [R]"),Ink,DockX+835,DockY+88,.9f);
    Text(PC->bReducedMotion?TEXT("V: SHAKE OFF"):TEXT("V: SHAKE ON"),Muted,DockX+960,DockY+88,.85f);
    if(bShowHelp)
    {
        Rect(Panel,W-426,68,402,140);
        Text(TEXT("RESTORE THE ROOM"),Ink,W-410,82,1.2f);
        Text(TEXT("Each fragment follows its matching colored face."),Muted,W-410,111);
        Text(TEXT("Turn in 90-degree steps. HINT gives target angles."),Muted,W-410,132);
        Text(TEXT("ASSEMBLE joins aligned parts and lowers them."),Muted,W-410,153);
        Text(TEXT("Restored furniture stays on the fixed floor."),Muted,W-410,174);
    }
}

bool ARoomShiftHUD::HandleClick(float X,float Y)
{
    auto* PC=Cast<ARoomShiftController>(PlayerOwner); auto* P=PC?PC->Puzzle():nullptr; if(!P)return false;
    X=X/UIScale-DockX; Y=Y/UIScale-DockY;
    if(X<0||X>1100||Y<0||Y>110)return false;
    if(Y>=82&&Y<=108)
    {
        if(X>=625&&X<=725)P->ScramblePuzzle();
        else if(X>=730&&X<=820)P->Undo();
        else if(X>=825&&X<=925)P->ResetPuzzle();
        else if(X>=945)PC->bReducedMotion=!PC->bReducedMotion;
        return true;
    }
    if(Y>=36&&Y<=76)
    {
        for(int32 I=0;I<P->FaceCount();++I) if(X>=14+112*I&&X<=118+112*I){P->SelectFace(I);return true;}
        if(X>=580&&X<=662)P->RotateFace(P->Selected,-1);
        else if(X>=668&&X<=750)P->RotateFace(P->Selected,1);
        else if(X>=762&&X<=930)P->Assemble();
        else if(X>=944&&X<=1008)P->Message=P->Hint();
        else if(X>=1014&&X<=1084)bShowHelp=!bShowHelp;
    }
    return true;
}

ARoomShiftMode::ARoomShiftMode()
{
    PlayerControllerClass=ARoomShiftController::StaticClass(); HUDClass=ARoomShiftHUD::StaticClass(); DefaultPawnClass=nullptr;
}
void ARoomShiftMode::BeginPlay()
{
    Super::BeginPlay();
    for(TActorIterator<ARoomShiftPuzzle> It(GetWorld());It;++It) return;
    GetWorld()->SpawnActor<ARoomShiftPuzzle>();
}
