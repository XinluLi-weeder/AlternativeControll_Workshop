// Local UDP transport only. Puzzle rules remain in HallwayGame/Animation.
#include "HallwayGame.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Camera/CameraActor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameViewportClient.h"
void AHallwayPuzzle::PollDevice()
{
    if(!Socket)return;uint32 Size=0;
    for(int32 N=0;N<128&&Socket->HasPendingData(Size);++N)
    {
        uint8 Buffer[2049];int32 Read=0;auto Sender=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();if(!Socket->RecvFrom(Buffer,2048,Read,*Sender)||Read<=0)continue;Buffer[Read]=0;
        TSharedPtr<FJsonObject> O;auto Reader=TJsonReaderFactory<>::Create(FString(UTF8_TO_TCHAR(reinterpret_cast<char*>(Buffer))));if(!FJsonSerializer::Deserialize(Reader,O)||!O.IsValid())continue;
        FString Session,Cmd;double SeqD;if(!O->TryGetStringField(TEXT("session"),Session)||Session.IsEmpty()||Session.Len()>64||!O->TryGetStringField(TEXT("cmd"),Cmd)||!O->TryGetNumberField(TEXT("seq"),SeqD)||!FMath::IsFinite(SeqD)||SeqD<0||SeqD>2147483647||SeqD!=FMath::FloorToDouble(SeqD))continue;
        const int64 Seq=static_cast<int64>(SeqD);FString Status=TEXT("invalid");bool Accepted=false;
        FString RunId;O->TryGetStringField(TEXT("run_id"),RunId);
        if(Cmd==TEXT("status"))Status=TEXT("ok");
        else if(Cmd==TEXT("ui"))
        {
            FString Action;O->TryGetStringField(TEXT("action"),Action);
            if(Action==TEXT("start")){StartGame();Status=TEXT("ok");}
            else if(Action==TEXT("menu")){OpenMenu();Status=TEXT("ok");}
            else if(Action==TEXT("settings")){OpenMenu();bSettingsOpen=true;Status=TEXT("ok");}
            else if(Action==TEXT("mouse")){SetPhysicalControl(false);Status=TEXT("ok");}
            else if(Action==TEXT("controller")){SetPhysicalControl(true);Status=TEXT("ok");}
#if !UE_BUILD_SHIPPING
            else if(Action==TEXT("click"))
            {
                double X,Y;auto* PC=GetWorld()->GetFirstPlayerController();
                auto* HUD=PC?Cast<AHallwayHUD>(PC->GetHUD()):nullptr;
                if(HUD&&O->TryGetNumberField(TEXT("x"),X)&&O->TryGetNumberField(TEXT("y"),Y)&&HUD->HandleClick(X,Y))Status=TEXT("ok");
            }
            else if(Action==TEXT("screenshot")){FScreenshotRequest::RequestScreenshot(TEXT("RoomShiftQA"),true,true);Status=TEXT("ok");}
#endif
        }
        else if(!RunId.IsEmpty()&&RunId!=DeviceRunId)Status=TEXT("stale_run");
        else if(const int64* Prev=DeviceSequences.Find(Session);Prev&&Seq<=*Prev)Status=TEXT("duplicate");
        else
        {
            double Epoch=-1;O->TryGetNumberField(TEXT("stage_epoch"),Epoch);
            if(Cmd!=TEXT("debug")&&(!bGameStarted||bMenuOpen||!bPhysicalControl))Status=TEXT("inactive_control");
            else if(Cmd!=TEXT("view")&&Cmd!=TEXT("debug")&&Epoch!=StageEpoch)Status=TEXT("stale_stage");
            else if(Transitioning&&Cmd!=TEXT("debug"))Status=TEXT("transition");
            else if(Busy()&&Cmd!=TEXT("view")&&Cmd!=TEXT("debug"))Status=TEXT("busy");
            else if(Cmd==TEXT("debug"))
            {
                FString Text;if(O->TryGetStringField(TEXT("text"),Text))
                {
                    Text.ReplaceInline(TEXT("\r"),TEXT(" "));Text.ReplaceInline(TEXT("\n"),TEXT(" "));
                    ControllerSerialLine=Text.Left(180);
                    FString Downstream;if(O->TryGetStringField(TEXT("downstream"),Downstream)&&!Downstream.IsEmpty())ControllerReplyLine=FString::Printf(TEXT("Unreal ACK: received  |  Unreal -> ESP32: %s"),*Downstream.Left(48));
                    else ControllerReplyLine=TEXT("Unreal ACK: received  |  no controller output yet");
                    Accepted=true;
                }
            }
            else if(Cmd==TEXT("view")){double Y,P;if(O->TryGetNumberField(TEXT("yaw"),Y)&&O->TryGetNumberField(TEXT("pitch"),P))Accepted=SetViewAngles(Y,P);}
            else if(Cmd==TEXT("rotate")){double F,D;if(O->TryGetNumberField(TEXT("face"),F)&&O->TryGetNumberField(TEXT("dir"),D)&&F>=0&&F<5&&F==FMath::FloorToDouble(F)&&(D==1||D==-1))Accepted=RotateFace(F,D);}
            else if(Cmd==TEXT("angles"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
                if(O->TryGetArrayField(TEXT("degrees"),Values)&&Values->Num()==5)
                {
                    TArray<float> Degrees;bool Valid=true;
                    for(const auto& Value:*Values){double D=0;Valid&=Value->TryGetNumber(D)&&FMath::IsFinite(D)&&FMath::Abs(D)<=360000;Degrees.Add(static_cast<float>(D));}
                    if(Valid)Accepted=SetFaceAngles(Degrees);
                }
            }
            else if(Cmd==TEXT("assemble"))Accepted=Assemble();
            else if(Cmd==TEXT("undo"))Accepted=Undo();
            else if(Cmd==TEXT("reset"))Accepted=ResetStage();
            if(Accepted){if(DeviceSequences.Num()>32)DeviceSequences.Empty();DeviceSequences.Add(Session,Seq);Status=TEXT("ok");bDeviceConnected=true;}
        }
        auto Reply=MakeShared<FJsonObject>();Reply->SetNumberField(TEXT("seq"),Seq);Reply->SetStringField(TEXT("status"),Status);Reply->SetStringField(TEXT("mode"),TEXT("hallway"));Reply->SetNumberField(TEXT("stage"),State.Stage);Reply->SetNumberField(TEXT("stage_epoch"),StageEpoch);Reply->SetNumberField(TEXT("completion_serial"),CompletionSerial);Reply->SetBoolField(TEXT("busy"),Busy());Reply->SetBoolField(TEXT("transitioning"),Transitioning);Reply->SetBoolField(TEXT("device_connected"),bDeviceConnected);Reply->SetStringField(TEXT("controller_serial_line"),ControllerSerialLine);Reply->SetStringField(TEXT("controller_reply_line"),ControllerReplyLine);
        Reply->SetStringField(TEXT("run_id"),DeviceRunId);
        Reply->SetBoolField(TEXT("game_started"),bGameStarted);
        Reply->SetBoolField(TEXT("menu_open"),bMenuOpen);
        Reply->SetBoolField(TEXT("settings_open"),bSettingsOpen);
        Reply->SetStringField(TEXT("control_mode"),bPhysicalControl?TEXT("controller"):TEXT("mouse"));
        Reply->SetBoolField(TEXT("controller_online"),ControllerOnline());
        Reply->SetStringField(TEXT("controller_status"),ControllerStatus());
        Reply->SetBoolField(TEXT("blackout_visible"),Blackout.IsValid()&&!Blackout->IsHidden());
        Reply->SetBoolField(TEXT("layout_valid"),LayoutValid);
        Reply->SetNumberField(TEXT("ready_item"),ReadyItem());
        Reply->SetNumberField(TEXT("view_yaw"),FRotator::NormalizeAxis(ViewRotation.Yaw-90.f));Reply->SetNumberField(TEXT("view_pitch"),ViewRotation.Pitch);
        TArray<TSharedPtr<FJsonValue>> Angles;for(int32 F=0;F<5;++F)Angles.Add(MakeShared<FJsonValueNumber>(State.Turns[State.Stage][F]*90.f));Reply->SetArrayField(TEXT("angles"),Angles);
        if(Camera){auto Loc=MakeShared<FJsonObject>();Loc->SetNumberField(TEXT("x"),Camera->GetActorLocation().X);Loc->SetNumberField(TEXT("y"),Camera->GetActorLocation().Y);Loc->SetNumberField(TEXT("z"),Camera->GetActorLocation().Z);Reply->SetObjectField(TEXT("camera"),Loc);}
        TArray<TSharedPtr<FJsonValue>> Turns,Joined,Enabled;for(int32 F=0;F<5;++F){Turns.Add(MakeShared<FJsonValueNumber>(State.Turns[State.Stage][F]));Enabled.Add(MakeShared<FJsonValueBoolean>(FaceEnabled(F)));}for(bool J:State.Joined)Joined.Add(MakeShared<FJsonValueBoolean>(J));Reply->SetArrayField(TEXT("turns"),Turns);Reply->SetArrayField(TEXT("joined"),Joined);Reply->SetArrayField(TEXT("enabled_faces"),Enabled);
        bool NoAck=false;O->TryGetBoolField(TEXT("no_ack"),NoAck);
        if(NoAck&&(Cmd==TEXT("angles")||Cmd==TEXT("view")||Cmd==TEXT("debug")))continue;
        FString Out;auto Writer=TJsonWriterFactory<>::Create(&Out);FJsonSerializer::Serialize(Reply,Writer);FTCHARToUTF8 Data(*Out);int32 Sent=0;Socket->SendTo(reinterpret_cast<const uint8*>(Data.Get()),Data.Length(),Sent,*Sender);
    }
}
