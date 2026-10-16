#include "RoomShiftGame.h"
#include "Modules/ModuleManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
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
    const TCHAR* FurnitureNames[3] = {TEXT("SOFA"), TEXT("TABLE"), TEXT("DOOR")};
    const TCHAR* FaceNames[3] = {TEXT("A / FRONT"), TEXT("B / RIGHT"), TEXT("C / LEFT")};
    int32 Mod4(int32 Value) { return (Value % 4 + 4) % 4; }
}

ARoomShiftPuzzle::ARoomShiftPuzzle()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
    for (int32 I=0; I<3; ++I)
    {
        Faces[I] = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Face_%d"),I));
        Faces[I]->SetupAttachment(RootComponent);
    }
    for (int32 I=0; I<3; ++I)
    {
        Assemblies[I] = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Assembly_%d"),I));
        Assemblies[I]->SetupAttachment(RootComponent);
    }
}

FLinearColor ARoomShiftPuzzle::FaceColor(int32 Face)
{
    const FLinearColor Colors[] = {FLinearColor(.95f,.36f,.12f),FLinearColor(.17f,.67f,.91f),FLinearColor(.47f,.8f,.48f)};
    return Colors[FMath::Clamp(Face,0,2)];
}

int32 ARoomShiftPuzzle::GoalFor(int32 Furniture, int32 Face)
{
    // -1 means this wall has no part in that assembly.
    static const int32 Goals[3][3] = {{1,3,-1},{2,-1,2},{-1,1,0}};
    return Goals[Furniture][Face];
}

FTransform ARoomShiftPuzzle::FaceTransform(int32 Face, int32 Quarter) const
{
    const FVector Centers[] = {FVector(530,0,320),FVector(60,530,320),FVector(60,-530,320)};
    const FVector Axes[] = {FVector::ForwardVector,FVector::RightVector,FVector::RightVector};
    return FTransform(FQuat(Axes[Face],FMath::DegreesToRadians(90.f*Quarter)),Centers[Face]);
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
    for(int32 I=0;I<3;++I)
    {
        Faces[I]->SetRelativeTransform(FaceTransform(I,0));
        const bool Front=I==0;
        auto* Disk=Box(FString::Printf(TEXT("RotatingWall_%d"),I),FVector::ZeroVector,FVector(600,600,14),FLinearColor(.31,.33,.31),Faces[I]);
        Disk->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        Disk->SetRelativeRotation(Front ? FRotator(90,0,0) : FRotator(0,0,90));
        SurfaceOwners.Add(Disk,I);
        for(int32 J=0;J<4;++J)
        {
            float A=PI*.5f*J;
            FVector P=Front?FVector(-12,275*FMath::Cos(A),275*FMath::Sin(A)):FVector(275*FMath::Cos(A),I==1?-12:12,275*FMath::Sin(A));
            auto* Mark=Box(FString::Printf(TEXT("WallMark_%d_%d"),I,J),P,Front?FVector(8,24,24):FVector(24,8,24),FaceColor(I),Faces[I]);
            SurfaceOwners.Add(Mark,I);
        }
        // A visible ring belongs to the selected face and is hit-testable with its disk.
        for(int32 J=0;J<64;++J)
        {
            const float A=2*PI*J/64.f;
            FVector P=Front?FVector(-16,306*FMath::Cos(A),306*FMath::Sin(A)):FVector(306*FMath::Cos(A),I==1?-16:16,306*FMath::Sin(A));
            auto* Ring=Box(FString::Printf(TEXT("SelectionRing_%d_%d"),I,J),P,Front?FVector(5,7,32):FVector(7,5,32),FaceColor(I)*.3f,Faces[I]);
            Ring->SetRelativeRotation(FQuat(Front?FVector::ForwardVector:FVector::RightVector,Front?A:-A));
            Ring->SetCastShadow(false); SurfaceOwners.Add(Ring,I);
        }
        auto* Label=NewObject<UTextRenderComponent>(this,*FString::Printf(TEXT("WallLabel%d"),I));
        AddInstanceComponent(Label); Label->SetupAttachment(RootComponent);
        Label->SetRelativeLocation(FaceTransform(I,0).GetLocation()+FVector(Front?-28:0,I==1?-28:I==2?28:0,285));
        Label->SetRelativeRotation(FRotator(0,Front?180:I==1?-90:90,0));
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
}

void ARoomShiftPuzzle::BeginPlay()
{
    Super::BeginPlay();
    BaseMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Surface.M_Surface"));
    if(!BaseMaterial) BaseMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    WoodMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Wood.M_Wood"));
    FabricMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Fabric.M_Fabric"));
    PlasterMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Plaster.M_Plaster"));
    RoundedMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/RoomShift/SM_RoundedBox.SM_RoundedBox"));
    if(!WoodMaterial)WoodMaterial=BaseMaterial; if(!FabricMaterial)FabricMaterial=BaseMaterial; if(!PlasterMaterial)PlasterMaterial=BaseMaterial;
    BuildRoom(); BuildFurniture(); UpdateHighlight();
    if(FParse::Param(FCommandLine::Get(),TEXT("RoomShiftShowSolved")))
    {
        FRoomSnapshot Solved; Solved.Turns[0]=1; Solved.Turns[1]=1; Solved.Turns[2]=2;
        Solved.Joined[0]=Solved.Joined[1]=Solved.Joined[2]=true; Restore(Solved);
    }
    Message=State.Joined[0]&&State.Joined[1]&&State.Joined[2]?TEXT("ROOM RESTORED / All furniture is now on the floor."):TEXT("Select a glowing wall. Align parts, then ASSEMBLE to lower them to the floor.");
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
    if(Busy() || Face<0 || Face>2 || (Direction!=1 && Direction!=-1)) return false;
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
            Message=State.Joined[0]&&State.Joined[1]&&State.Joined[2]?TEXT("ROOM RESTORED / All three objects are on the floor. RMB + WASD to explore."):TEXT("Furniture placed on the floor. It will stay here while you solve the next object.");
        }
    }
    PollDevice();
    // Fine colored tethers explain which wall carries each floating assembly group.
    for(int32 F=0;F<3;++F)
    {
        FVector Center=FVector::ZeroVector; int32 Count=0;
        for(const auto& P:Pieces)
            if(P.Furniture==F && !State.Joined[F] && P.Face==Selected) { Center+=P.Mesh->GetComponentLocation(); ++Count; }
        if(Count)
        {
            Center/=Count; FVector Mount=Center;
            if(Selected==0) Mount.X=515; else Mount.Y=Selected==1?515:-515;
            DrawDebugLine(GetWorld(),Center,Mount,FaceColor(Selected).ToFColor(true),false,-1,0,.7f);
        }
    }
}

int32 ARoomShiftPuzzle::ReadyFurniture() const
{
    if(Busy()) return -1;
    for(int32 F=0;F<3;++F)
    {
        if(State.Joined[F]) continue;
        bool Match=true;
        for(int32 I=0;I<3;++I) if(GoalFor(F,I)>=0 && GoalFor(F,I)!=State.Turns[I]) Match=false;
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
    Message=FString::Printf(TEXT("%s complete! Placing it on the floor..."),FurnitureNames[F]);
    return true;
}

void ARoomShiftPuzzle::Restore(const FRoomSnapshot& Snapshot)
{
    ActiveFace=-1; Landing=-1; State=Snapshot;
    for(int32 I=0;I<3;++I) Faces[I]->SetRelativeTransform(FaceTransform(I,State.Turns[I]));
    for(int32 I=0;I<3;++I) Assemblies[I]->SetRelativeLocation(State.Joined[I]?FVector(0,0,-AssemblyLift):FVector::ZeroVector);
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
    if(Face<0||Face>2)return;
    Selected=Face; UpdateHighlight();
    if(!Busy())Message=FString::Printf(TEXT("Selected %s / The glowing wall and highlighted fragments rotate together."),FaceNames[Face]);
}

void ARoomShiftPuzzle::UpdateHighlight()
{
    for(const auto& Entry:SurfaceOwners)
    {
        auto* Mesh=Cast<UStaticMeshComponent>(const_cast<UPrimitiveComponent*>(Entry.Key));
        auto* Material=Mesh?Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)):nullptr;
        if(!Material)continue;
        const bool SelectedWall=Entry.Value==Selected;
        const bool Ring=Mesh->GetName().Contains(TEXT("SelectionRing"));
        Material->SetVectorParameterValue(TEXT("HighlightColor"),FaceColor(Entry.Value));
        Material->SetScalarParameterValue(TEXT("Highlight"),SelectedWall?(Ring?1.f:.035f):(Ring?.008f:0.f));
        if(!Ring) Material->SetVectorParameterValue(TEXT("Tint"),SelectedWall?FLinearColor(.31,.33,.31)+FaceColor(Entry.Value)*.18f:FLinearColor(.31,.33,.31));
    }
    for(const auto& P:Pieces)
    {
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
    for(int32 F=0;F<3;++F) if(!State.Joined[F])
    {
        FString Result=FString::Printf(TEXT("%s / "),FurnitureNames[F]);
        for(int32 I=0;I<3;++I) if(GoalFor(F,I)>=0) Result+=FString::Printf(TEXT("%c = %d deg   "),TCHAR('A'+I),GoalFor(F,I)*90);
        return Result+TEXT("then ASSEMBLE.");
    }
    return TEXT("All restored. Completed furniture stays on the floor. RMB + WASD to explore.");
}

void ARoomShiftPuzzle::PollDevice()
{
    if(!Socket) return;
    uint32 Size=0;
    // Bounded polling: malformed or excessive traffic cannot monopolize a frame.
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
        if(const int64* Last=DeviceSequences.Find(Session);Last&&Seq<=*Last) Status=TEXT("duplicate");
        else if(Busy()) Status=TEXT("busy");
        else
        {
            bool Accepted=false;
            if(Cmd==TEXT("rotate"))
            {
                double Face,Dir;
                if(Obj->TryGetNumberField(TEXT("face"),Face)&&Obj->TryGetNumberField(TEXT("dir"),Dir)&&Face>=0&&Face<=2&&Face==FMath::FloorToDouble(Face)&&(Dir==1||Dir==-1)) Accepted=RotateFace(static_cast<int32>(Face),static_cast<int32>(Dir));
            }
            else if(Cmd==TEXT("assemble")) Accepted=Assemble();
            else if(Cmd==TEXT("undo")) { Undo(); Accepted=true; }
            else if(Cmd==TEXT("reset")) { ResetPuzzle(); Accepted=true; }
            if(Accepted)
            {
                if(DeviceSequences.Num()>32) DeviceSequences.Empty();
                DeviceSequences.Add(Session,Seq); Status=TEXT("ok");
                DeviceStatus=TEXT("DEVICE  /  connected via local bridge");
            }
        }
        FString Reply=FString::Printf(TEXT("{\"seq\":%lld,\"status\":\"%s\",\"turns\":[%d,%d,%d],\"joined\":[%s,%s,%s]}"),Seq,*Status,State.Turns[0],State.Turns[1],State.Turns[2],State.Joined[0]?TEXT("true"):TEXT("false"),State.Joined[1]?TEXT("true"):TEXT("false"),State.Joined[2]?TEXT("true"):TEXT("false"));
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
    Check(!RotateFace(-1,1)&&!RotateFace(0,0),TEXT("reject-invalid-command"));
    const FTransform Initial=Faces[0]->GetComponentTransform();
    for(int32 I=0;I<4;++I){ Check(RotateFace(0,1),TEXT("quarter-turn")); Tick(1.f); }
    Check(Faces[0]->GetComponentTransform().Equals(Initial,.001f),TEXT("full-circle"));
    Restore(FRoomSnapshot()); History.Empty();
    FRoomSnapshot S; S.Turns[0]=1; S.Turns[1]=3; Restore(S);
    TArray<FTransform> Before; for(const auto& P:Pieces) Before.Add(P.Mesh->GetComponentTransform());
    Check(ReadyFurniture()==0&&Assemble(),TEXT("sofa-assembly"));
    for(int32 I=0;I<Pieces.Num();++I) Check(Pieces[I].Mesh->GetComponentTransform().Equals(Before[I],.01f),TEXT("assembly-preserves-pose"));
    Check(!RotateFace(0,1),TEXT("reject-during-landing")); Tick(2.f);
    Before.Empty(); for(const auto& P:Pieces) Before.Add(P.Mesh->GetComponentTransform());
    Check(RotateFace(1,1),TEXT("donor-turn")); Check(!RotateFace(2,1),TEXT("reject-during-animation")); Tick(1.f);
    for(int32 I=0;I<Pieces.Num();++I) if(Pieces[I].Furniture==0) Check(Pieces[I].Mesh->GetComponentTransform().Equals(Before[I],.01f),TEXT("sofa-detached-from-donor"));
    Undo(); Undo(); Check(!State.Joined[0]&&State.Turns[1]==3,TEXT("undo-restores-ownership"));
    for(const auto& P:Pieces) Check(P.Mesh->GetAttachParent()==Faces[P.Face],TEXT("original-parent"));
    S=FRoomSnapshot(); S.Turns[0]=1; S.Turns[1]=3; Restore(S); Check(Assemble(),TEXT("solve-sofa"));
    S=State; S.Turns[0]=2; S.Turns[2]=2; Restore(S); Check(Assemble(),TEXT("solve-table"));
    S=State; S.Turns[1]=1; S.Turns[2]=0; Restore(S); Check(Assemble(),TEXT("solve-door"));
    Tick(2.f);
    Check(State.Joined[0]&&State.Joined[1]&&State.Joined[2],TEXT("complete-level"));
    for(int32 F=0;F<3;++F)
    {
        float Lowest=MAX_flt;
        for(const auto& P:Pieces)if(P.Furniture==F)
        {
            Lowest=FMath::Min(Lowest,static_cast<float>(P.Mesh->Bounds.Origin.Z-P.Mesh->Bounds.BoxExtent.Z));
            Check(P.Mesh->GetAttachParent()==Assemblies[F],TEXT("assembled-floor-parent"));
        }
        Check(FMath::Abs(Lowest)<.1f,TEXT("furniture-touches-floor"));
    }
    Before.Empty();for(const auto& P:Pieces)Before.Add(P.Mesh->GetComponentTransform());
    for(int32 I=0;I<3;++I){RotateFace(I,1);Tick(1.f);}
    for(int32 I=0;I<Pieces.Num();++I)Check(Pieces[I].Mesh->GetComponentTransform().Equals(Before[I],.01f),TEXT("floor-objects-independent-of-all-walls"));
    const auto Completed=State; Restore(Completed);
    for(int32 I=0;I<Pieces.Num();++I)Check(Pieces[I].Mesh->GetComponentTransform().Equals(Before[I],.01f),TEXT("restore-completed-pose"));
    SelectFace(2);
    for(const auto& Entry:SurfaceOwners)if(Entry.Key->GetName().Contains(TEXT("SelectionRing")))
    {
        auto* Material=Cast<UMaterialInstanceDynamic>(CastChecked<UStaticMeshComponent>(Entry.Key)->GetMaterial(0));
        Check(Material&&Material->K2_GetScalarParameterValue(TEXT("Highlight"))==(Entry.Value==2?1.f:.008f),TEXT("selected-ring-highlight"));
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
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>(FVector(-550,0,260),ViewRotation);
    Camera->GetCameraComponent()->FieldOfView=108;
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
void ARoomShiftController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime); auto* P=Puzzle(); if(!P)return;
    const bool Right=IsInputKeyDown(EKeys::RightMouseButton);
    const bool Middle=IsInputKeyDown(EKeys::MiddleMouseButton);
    const bool Navigating=Right||Middle;
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
            GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
        }
    }
    if(Right)
    {
        float X,Y; GetInputMouseDelta(X,Y);
        ViewRotation.Yaw+=X*.24f; ViewRotation.Pitch=FMath::Clamp(ViewRotation.Pitch+Y*.24f,-85.f,85.f);
        if(GetViewTarget()) GetViewTarget()->SetActorRotation(ViewRotation);
    }
    auto* Camera=Cast<ACameraActor>(GetViewTarget());
    if(Camera)
    {
        FVector Move=FVector::ZeroVector;
        if(Right)
        {
            Move+=Camera->GetActorForwardVector()*(float(IsInputKeyDown(EKeys::W))-float(IsInputKeyDown(EKeys::S)));
            Move+=Camera->GetActorRightVector()*(float(IsInputKeyDown(EKeys::D))-float(IsInputKeyDown(EKeys::A)));
            Move.Z+=float(IsInputKeyDown(EKeys::SpaceBar))-float(IsInputKeyDown(EKeys::LeftControl));
            Move=Move.GetClampedToMaxSize(1.f)*DeltaTime*(IsInputKeyDown(EKeys::LeftShift)?600.f:260.f);
        }
        else if(Middle)
        {
            float X,Y; GetInputMouseDelta(X,Y);
            Move=Camera->GetActorRightVector()*(-X*.7f)+Camera->GetActorUpVector()*Y*.7f;
        }
        FVector Position=Camera->GetActorLocation()+Move;
        Position.X=FMath::Clamp(Position.X,-600.f,490.f);Position.Y=FMath::Clamp(Position.Y,-500.f,500.f);Position.Z=FMath::Clamp(Position.Z,80.f,600.f);
        Camera->SetActorLocation(Position);
        float Fov=Camera->GetCameraComponent()->FieldOfView;
        if(WasInputKeyJustPressed(EKeys::MouseScrollUp))Fov-=5;
        if(WasInputKeyJustPressed(EKeys::MouseScrollDown))Fov+=5;
        Camera->GetCameraComponent()->FieldOfView=FMath::Clamp(Fov,65.f,115.f);
        if(WasInputKeyJustPressed(EKeys::Home))
        {
            ViewRotation=FRotator(-12,0,0);Camera->SetActorLocationAndRotation(FVector(-550,0,260),ViewRotation);Camera->GetCameraComponent()->FieldOfView=108;
        }
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
    if(WasInputKeyJustPressed(EKeys::Q))P->RotateFace(P->Selected,-1);
    if(WasInputKeyJustPressed(EKeys::E))P->RotateFace(P->Selected,1);
    if(!Navigating&&WasInputKeyJustPressed(EKeys::SpaceBar))P->Assemble();
    if(WasInputKeyJustPressed(EKeys::Z))P->Undo();
    if(WasInputKeyJustPressed(EKeys::R))P->ResetPuzzle();
    if(WasInputKeyJustPressed(EKeys::H))P->Message=P->Hint();
}

void ARoomShiftHUD::Button(float X,float Y,float W,const FString& Label,bool Enabled)
{
    const float Scale=FMath::Min(Canvas->SizeX/1440.f,Canvas->SizeY/900.f);
    X*=Scale; Y*=Scale; W*=Scale;
    DrawRect(Enabled?FLinearColor(.13,.2,.27,.96):FLinearColor(.08,.1,.12,.8),X,Y,W,36*Scale);
    DrawText(Label,Enabled?FLinearColor(.9,.94,1):FLinearColor(.35,.4,.45),X+12*Scale,Y+8*Scale,GEngine->GetMediumFont(),.8f*Scale);
}
void ARoomShiftHUD::DrawHUD()
{
    Super::DrawHUD(); auto* PC=Cast<ARoomShiftController>(PlayerOwner); auto* P=PC?PC->Puzzle():nullptr; if(!P)return;
    const float Scale=FMath::Min(Canvas->SizeX/1440.f,Canvas->SizeY/900.f);
    UIScale=Scale;
    auto DrawRect=[this,Scale](FLinearColor C,float X,float Y,float W,float H){ AHUD::DrawRect(C,X*Scale,Y*Scale,W*Scale,H*Scale); };
    auto DrawText=[this,Scale](const FString& T,FLinearColor C,float X,float Y,UFont* F,float S){ AHUD::DrawText(T,C,X*Scale,Y*Scale,F,S*Scale); };
    DrawRect(FLinearColor(.025,.04,.06,.92),20,20,310,330);
    DrawText(TEXT("ROOM / SHIFT"),FLinearColor::White,38,32,GEngine->GetLargeFont(),.8f);
    DrawText(TEXT("Rotate the room. Restore the objects."),FLinearColor(.6,.72,.8),38,67,GEngine->GetSmallFont(),1.f);
    for(int32 I=0;I<3;++I)
    {
        float Y=104+I*52;
        DrawRect(I==P->Selected?ARoomShiftPuzzle::FaceColor(I)*.3f:FLinearColor(.07,.1,.14,.9),34,Y,282,44);
        DrawRect(ARoomShiftPuzzle::FaceColor(I),34,Y,4,44);
        DrawText(FString::Printf(TEXT("%s     %03d deg"),FaceNames[I],P->State.Turns[I]*90),FLinearColor::White,49,Y+11,GEngine->GetMediumFont(),.85f);
    }
    Button(34,270,134,TEXT("<  TURN -90"),!P->Busy()); Button(182,270,134,TEXT("TURN +90  >"),!P->Busy());
    DrawText(FString::Printf(TEXT("SELECTED: %s"),FaceNames[P->Selected]),ARoomShiftPuzzle::FaceColor(P->Selected),36,319,GEngine->GetSmallFont(),1.f);
    DrawRect(FLinearColor(.025,.04,.06,.92),20,365,310,165);
    for(int32 I=0;I<3;++I)
        DrawText(FString::Printf(TEXT("%s  %s   / %s"),P->State.Joined[I]?TEXT("[OK]"):TEXT("[  ]"),FurnitureNames[I],P->State.Joined[I]?TEXT("FLOOR"):TEXT("FRAGMENTS")),P->State.Joined[I]?FLinearColor(.5,1,.65):FLinearColor(.8,.85,.9),38,380+I*28,GEngine->GetMediumFont(),.8f);
    Button(34,481,282,TEXT("ASSEMBLE ALIGNED PARTS"),P->ReadyFurniture()>=0);
    Button(20,545,96,TEXT("UNDO")); Button(127,545,96,TEXT("RESET")); Button(234,545,96,TEXT("HINT"));
    const float Y=Canvas->SizeY/Scale-94;
    DrawRect(FLinearColor(.02,.035,.05,.93),20,Y,Canvas->SizeX/Scale-40,74);
    DrawText(P->Message,FLinearColor(.95,.86,.65),35,Y+10,GEngine->GetMediumFont(),.8f);
    DrawText(FString::Printf(TEXT("RMB: look + WASD move + SPACE/CTRL rise/fall  |  MMB: pan  |  Wheel: zoom  |  HOME: reset view  |  Turns %d"),P->State.Moves),FLinearColor(.62,.73,.8),35,Y+40,GEngine->GetSmallFont(),1.f);
    DrawText(P->DeviceStatus,FLinearColor(.5,.65,.73),Canvas->SizeX/Scale-360,25,GEngine->GetSmallFont(),.9f);
}
bool ARoomShiftHUD::HandleClick(float X,float Y)
{
    auto* PC=Cast<ARoomShiftController>(PlayerOwner);auto* P=PC?PC->Puzzle():nullptr;if(!P)return false;
    // Canvas is cleared by GameViewportClient after drawing; input runs outside that scope.
    X/=UIScale; Y/=UIScale;
    if(X<20||X>330)return false;
    if(X>=34&&X<=316)
    {
        for(int32 I=0;I<3;++I)if(Y>=104+I*52&&Y<=148+I*52){P->SelectFace(I);return true;}
        if(Y>=270&&Y<=306){P->RotateFace(P->Selected,X<175?-1:1);return true;}
        if(Y>=481&&Y<=517){P->Assemble();return true;}
    }
    if(Y>=545&&Y<=581){if(X<116)P->Undo();else if(X<223)P->ResetPuzzle();else P->Message=P->Hint();return true;}
    return Y>=20&&Y<=581;
}

ARoomShiftMode::ARoomShiftMode()
{
    PlayerControllerClass=ARoomShiftController::StaticClass(); HUDClass=ARoomShiftHUD::StaticClass(); DefaultPawnClass=nullptr;
}
void ARoomShiftMode::BeginPlay()
{
    Super::BeginPlay(); GetWorld()->SpawnActor<ARoomShiftPuzzle>();
}
