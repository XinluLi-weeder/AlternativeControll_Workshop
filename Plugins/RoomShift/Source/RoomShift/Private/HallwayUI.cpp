// Mouse/keyboard routing and HUD presentation; no puzzle transform ownership.
#include "HallwayGame.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"
AHallwayPuzzle* AHallwayController::Puzzle() const{for(TActorIterator<AHallwayPuzzle> It(GetWorld());It;++It)return *It;return nullptr;}
void AHallwayController::BeginPlay(){Super::BeginPlay();bShowMouseCursor=true;FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);}
void AHallwayController::PlayerTick(float Dt)
{
    Super::PlayerTick(Dt);auto* P=Puzzle();if(!P)return;
    if(WasInputKeyJustPressed(EKeys::Escape))
    {
        if(P->bMenuOpen&&P->bGameStarted)P->StartGame();else P->OpenMenu();
    }
    if(P->bMenuOpen)
    {
        if(ActiveInputMode!=0){Looking=false;bShowMouseCursor=true;FInputModeGameAndUI Mode;SetInputMode(Mode);ActiveInputMode=0;}
        if(WasInputKeyJustPressed(EKeys::LeftMouseButton)){float X,Y;GetMousePosition(X,Y);if(auto* H=Cast<AHallwayHUD>(GetHUD()))H->HandleClick(X,Y);}
        return;
    }
    if(P->bPhysicalControl){if(ActiveInputMode!=1){Looking=false;bShowMouseCursor=false;FInputModeGameOnly Mode;Mode.SetConsumeCaptureMouseDown(false);SetInputMode(Mode);ActiveInputMode=1;}return;}
    if(ActiveInputMode!=2){Looking=false;bShowMouseCursor=true;FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);ActiveInputMode=2;}
    const bool Right=IsInputKeyDown(EKeys::RightMouseButton);
    if(Right!=Looking){Looking=Right;bShowMouseCursor=!Right;if(Right){FInputModeGameOnly M;M.SetConsumeCaptureMouseDown(false);SetInputMode(M);}else{FInputModeGameAndUI M;M.SetHideCursorDuringCapture(false);SetInputMode(M);}}
    if(P->Transitioning)return;
    if(Right){float X,Y;GetInputMouseDelta(X,Y);const FRotator R=GetViewTarget()->GetActorRotation();P->SetViewAngles(R.Yaw-90+X*.24f,R.Pitch+Y*.24f);}
    if(WasInputKeyJustPressed(EKeys::Home)&&GetViewTarget())
    {
        const FVector Center=P->State.Stage==1?P->BedroomCenter:P->EntranceCenter;
        const FRotator Home=P->bStartInBedroom?FRotator(P->ActorHasTag(TEXT("RH_HIGH_VIEW"))?-18.f:8.f,90,0):(Center-GetViewTarget()->GetActorLocation()).Rotation();
        P->SetViewAngles(Home.Yaw-90.f,Home.Pitch);
    }
    if(!Right&&WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        float X,Y;GetMousePosition(X,Y);auto* H=Cast<AHallwayHUD>(GetHUD());if(!H||!H->HandleClick(X,Y)){FHitResult Hit;if(GetHitResultUnderCursor(ECC_Visibility,false,Hit))P->SelectFace(P->FaceFor(Hit.GetActor()));}
    }
    if(WasInputKeyJustPressed(EKeys::F3))P->bShowControllerDebug=!P->bShowControllerDebug;
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five};for(int32 I=0;I<5;++I)if(WasInputKeyJustPressed(Keys[I]))P->SelectFace(I);
    if(WasInputKeyJustPressed(EKeys::Q))P->RotateFace(P->Selected,-1);if(WasInputKeyJustPressed(EKeys::E))P->RotateFace(P->Selected,1);
    if(WasInputKeyJustPressed(EKeys::SpaceBar))P->Assemble();if(WasInputKeyJustPressed(EKeys::Z))P->Undo();if(WasInputKeyJustPressed(EKeys::R))P->ResetStage();
    if(WasInputKeyJustPressed(EKeys::H)){P->Message=P->Hint();P->MessageTime=8;}
}
void AHallwayHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas)return;auto* PC=Cast<AHallwayController>(PlayerOwner);auto* P=PC?PC->Puzzle():nullptr;if(!P)return;
    Scale=FMath::Min(Canvas->SizeX/1280.f,Canvas->SizeY/720.f);Left=(Canvas->SizeX-880*Scale)*.5f;Top=Canvas->SizeY-102*Scale;
    auto Text=[&](const FString& S,float X,float Y,FLinearColor C,float Z=1.f){DrawText(S,C,X,Y,GEngine->GetMediumFont(),Scale*Z);};
    if(P->bMenuOpen)
    {
        DrawRect(FLinearColor(.01,.015,.025,.8),0,0,Canvas->SizeX,Canvas->SizeY);
        const float X=Canvas->SizeX*.5f-260*Scale,Y=Canvas->SizeY*.5f-190*Scale;
        DrawRect(FLinearColor(.055,.07,.09,.98),X,Y,520*Scale,380*Scale);
        DrawRect(FLinearColor(.75,.58,.36,1),X,Y,520*Scale,3*Scale);
        Text(P->bSettingsOpen?TEXT("CONTROLS"):TEXT("ROOM SHIFT"),X+40*Scale,Y+32*Scale,FLinearColor(.96,.9,.8),1.8f);
        Text(P->bSettingsOpen?TEXT("Choose how to restore the room"):TEXT("Turn the room. Bring the pieces home."),X+40*Scale,Y+76*Scale,FLinearColor(.65,.73,.8),.85f);
        const FString Labels[]={P->bSettingsOpen?(P->bPhysicalControl?TEXT("Mouse + keyboard"):TEXT("Mouse + keyboard   [selected]")):(P->bGameStarted?TEXT("Resume"):TEXT("Start playing")),P->bSettingsOpen?(P->bPhysicalControl?TEXT("Physical controller   [selected]"):TEXT("Physical controller")):TEXT("Settings"),P->bSettingsOpen?TEXT("Back"):TEXT("Quit")};
        float MX=0,MY=0;PC->GetMousePosition(MX,MY);
        for(int32 I=0;I<3;++I)
        {
            const float BY=Y+(115+58*I)*Scale;
            const bool Hover=MX>=X+40*Scale&&MX<=X+480*Scale&&MY>=BY&&MY<=BY+46*Scale;
            DrawRect(Hover?FLinearColor(.23,.31,.32,1):FLinearColor(.12,.17,.2,1),X+40*Scale,BY,440*Scale,46*Scale);
            Text(Labels[I],X+56*Scale,BY+13*Scale,FLinearColor(.94,.94,.87),.95f);
        }
        Text(P->ControllerStatus(),X+24*Scale,Y+306*Scale,P->ControllerOnline()?FLinearColor(.5,1,.7):FLinearColor(.9,.77,.52),.65f);
        Text(P->bPhysicalControl?TEXT("Turn faces / tilt to look / shake aligned furniture home"):TEXT("RMB look   /   click a face   /   Q, E turn   /   Space assemble"),X+24*Scale,Y+336*Scale,FLinearColor(.65,.73,.8),.63f);
        return;
    }
    Text(FString::Printf(TEXT("%s   %d / %d"),P->State.Stage==0?TEXT("01  ENTRY"):TEXT("02  BEDROOM"),P->StageProgress(),P->StageItemCount(P->State.Stage)),24*Scale,24*Scale,FLinearColor(.9,.95,1),1.1f);
    if(P->bShowControllerDebug)
    {
        const float BoxX=24*Scale,BoxY=50*Scale,BoxW=FMath::Min(620*Scale,Canvas->SizeX-48*Scale),BoxH=48*Scale;
        DrawRect(FLinearColor(.015,.025,.04,.78),BoxX,BoxY,BoxW,BoxH);
        const FLinearColor LinkColor=P->bDeviceConnected?FLinearColor(.45,1,.62):FLinearColor(1,.72,.35);
        Text(FString::Printf(TEXT("ESP32 -> Unreal: %s"),*P->ControllerSerialLine.Left(92)),BoxX+8*Scale,BoxY+5*Scale,LinkColor,.63f);
        Text(P->ControllerReplyLine.Left(104),BoxX+8*Scale,BoxY+25*Scale,FLinearColor(.72,.82,.95),.60f);
    }
    if(P->MessageTime>0){DrawRect(FLinearColor(.025,.035,.05,.82),Canvas->SizeX*.15f,72*Scale,Canvas->SizeX*.7f,44*Scale);Text(P->Message,Canvas->SizeX*.15f+16*Scale,84*Scale,FLinearColor(1,.84,.42),.95f);}
    if(P->MessageTime>0.f&&P->MessageTime<=3.f&&P->Message.Contains(TEXT("RESTORED")))
    {
        const float Age=3.f-P->MessageTime;
        const float Flash=FMath::Clamp(1.f-Age/.45f,0.f,1.f);
        if(Flash>0)DrawRect(FLinearColor(1.f,.72f,.18f,.16f*Flash),0,0,Canvas->SizeX,Canvas->SizeY);
        const float Alpha=FMath::Clamp(1.f-FMath::Max(0.f,Age-1.6f)/1.2f,0.f,1.f);
        if(Alpha>0)
        {
            const float W=FMath::Min(Canvas->SizeX*.66f,780*Scale);
            const float H=64*Scale;
            const float X=(Canvas->SizeX-W)*.5f;
            const float Y=Canvas->SizeY*.17f;
            DrawRect(FLinearColor(.03f,.04f,.05f,.78f*Alpha),X,Y,W,H);
            DrawRect(FLinearColor(1.f,.72f,.18f,.95f*Alpha),X,Y,W,3*Scale);
            Text(P->Message,X+20*Scale,Y+14*Scale,FLinearColor(1,.86,.45,Alpha),1.1f);
            Text(FString::Printf(TEXT("%d / %d restored"),P->StageProgress(),P->StageItemCount(P->State.Stage)),X+20*Scale,Y+42*Scale,FLinearColor(.75,.82,.9,Alpha),.72f);
        }
    }
    if(P->Transitioning)return;
    if(P->bPhysicalControl)
    {
        Text(P->ControllerStatus(),24*Scale,52*Scale,P->ControllerOnline()?FLinearColor(.5,1,.7):FLinearColor(1,.65,.3),.8f);
        Text(P->ReadyItem()>=0?TEXT("Aligned! Hold steady, then shake to restore."):TEXT("Turn a face to align the furniture. Tilt the box to look."),24*Scale,Canvas->SizeY-58*Scale,FLinearColor(.9,.85,.7),.85f);
        Text(TEXT("ESC  Settings / Quit"),24*Scale,Canvas->SizeY-30*Scale,FLinearColor(.65,.72,.8),.75f);
        return;
    }
    DrawRect(FLinearColor(.02,.03,.045,.88),Left-12*Scale,Top-10*Scale,904*Scale,102*Scale);
    for(int32 I=0;I<5;++I){const bool On=P->FaceEnabled(I);const float X=Left+I*112*Scale;DrawRect(P->FaceColor(I),X,Top-4*Scale,106*Scale,3*Scale);DrawRect(P->Selected==I?FLinearColor(.09,.32,.50,.95):FLinearColor(.07,.09,.12,.9),X,Top,106*Scale,42*Scale);Text(P->FaceName(I),X+7*Scale,Top+5*Scale,On?FLinearColor(.85,.92,1):FLinearColor(.3,.32,.35),.7f);Text(On?FString::Printf(TEXT("%.1f deg"),P->State.Turns[P->State.Stage][I]*90):TEXT("LOCKED"),X+7*Scale,Top+23*Scale,FLinearColor(.7,.75,.8),.7f);}
    const TCHAR* Buttons[]={TEXT("Q  -90"),TEXT("E  +90"),TEXT("SPACE  ASSEMBLE")};const float Xs[]={565,650,735};const float Widths[]={80,80,145};
    for(int32 I=0;I<3;++I){const bool Enabled=!P->Busy()&&(I<2||P->ReadyItem()>=0);DrawRect(Enabled?FLinearColor(.18,.28,.27,.95):FLinearColor(.07,.09,.12,.9),Left+Xs[I]*Scale,Top,Widths[I]*Scale,42*Scale);Text(Buttons[I],Left+(Xs[I]+7)*Scale,Top+14*Scale,Enabled?FLinearColor(.95,.86,.55):FLinearColor(.35,.38,.4),.72f);}
    Text(TEXT("RMB look   |   H hint   |   Z undo   |   R reset   |   HOME center   |   ESC settings / quit"),Left,Top+58*Scale,FLinearColor(.65,.72,.8),.78f);
}
bool AHallwayHUD::HandleClick(float X,float Y)
{
    auto* PC=Cast<AHallwayController>(PlayerOwner);auto* P=PC?PC->Puzzle():nullptr;if(!P||Scale<=0)return false;
    if(P->bMenuOpen)
    {
        // Canvas only exists during DrawHUD; input runs outside that lifetime.
        int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);
        if(Width<=0||Height<=0)return false;
        const float InputScale=FMath::Min(Width/1280.f,Height/720.f);
        const float MX=(X-Width*.5f)/InputScale+260,MY=(Y-Height*.5f)/InputScale+190;
        if(MX>=40&&MX<=480)for(int32 I=0;I<3;++I)if(MY>=115+58*I&&MY<=161+58*I)
        {
            if(P->bSettingsOpen){if(I<2)P->SetPhysicalControl(I==1);else P->bSettingsOpen=false;}
            else if(I==0)P->StartGame();else if(I==1)P->bSettingsOpen=true;
            else UKismetSystemLibrary::QuitGame(this,PC,EQuitPreference::Quit,false);
            break;
        }
        return true;
    }
    if(P->bPhysicalControl)return true;
    X=(X-Left)/Scale;Y=(Y-Top)/Scale;
    if(Y<0||Y>92||X< -12||X>892)return false;
    if(Y<=42){if(X>=0&&X<560)P->SelectFace(static_cast<int32>(X/112));else if(X>=565&&X<645)P->RotateFace(P->Selected,-1);else if(X>=650&&X<730)P->RotateFace(P->Selected,1);else if(X>=735&&X<880)P->Assemble();}return true;
}
AHallwayMode::AHallwayMode(){PlayerControllerClass=AHallwayController::StaticClass();HUDClass=AHallwayHUD::StaticClass();DefaultPawnClass=nullptr;}
