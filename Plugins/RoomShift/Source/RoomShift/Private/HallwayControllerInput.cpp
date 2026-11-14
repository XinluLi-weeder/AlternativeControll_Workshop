// Native receiver for the existing new_rotate.ino OSC messages. No Python runtime required.
#include "HallwayGame.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/PlatformTime.h"

namespace
{
bool ReadString(const uint8* Data,int32 Size,int32& Offset,FString& Out)
{
    const int32 Begin=Offset;
    while(Offset<Size&&Data[Offset])++Offset;
    if(Offset>=Size||Offset-Begin>80)return false;
    Out=FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(Data+Begin)));
    Offset=(Offset+4)&~3;return Offset<=Size;
}
bool DecodeOSC(const uint8* Data,int32 Size,FString& Address,TArray<float>& Values)
{
    int32 Offset=0;FString Tags;
    if(!ReadString(Data,Size,Offset,Address)||!ReadString(Data,Size,Offset,Tags))return false;
    const int32 Count=Address==TEXT("/rotary")?5:Address==TEXT("/imu")?4:0;
    if(!Count||Tags!=FString(TEXT(","))+FString::ChrN(Count,TEXT('f'))||Size-Offset!=4*Count)return false;
    for(int32 I=0;I<Count;++I,Offset+=4)
    {
        const uint32 Bits=(uint32(Data[Offset])<<24)|(uint32(Data[Offset+1])<<16)|(uint32(Data[Offset+2])<<8)|uint32(Data[Offset+3]);
        float Value;FMemory::Memcpy(&Value,&Bits,4);if(!FMath::IsFinite(Value))return false;Values.Add(Value);
    }
    return true;
}
}
void AHallwayPuzzle::ResetOSCReference()
{
    bRotaryReference=false;bViewReference=false;ShakePeaks=0;bShakeArmed=true;
}
void AHallwayPuzzle::StartGame()
{
    bGameStarted=true;bMenuOpen=false;bSettingsOpen=false;ResetOSCReference();++StageEpoch;
}
void AHallwayPuzzle::OpenMenu(){bMenuOpen=true;bSettingsOpen=false;ResetOSCReference();}
void AHallwayPuzzle::SetPhysicalControl(bool Enabled)
{
    bPhysicalControl=Enabled;ResetOSCReference();++StageEpoch;
    if(!Enabled&&OSCSocket){OSCSocket->Close();ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(OSCSocket);OSCSocket=nullptr;}
    LastRotaryPacket=LastIMUPacket=-100;NextOSCBind=0;bOSCBindFailed=false;bDeviceConnected=false;
}
bool AHallwayPuzzle::ControllerOnline() const
{
    const double Now=FPlatformTime::Seconds();
    return bPhysicalControl&&Now-LastRotaryPacket<2&&Now-LastIMUPacket<2;
}
FString AHallwayPuzzle::ControllerStatus() const
{
    if(!bPhysicalControl)return TEXT("Mouse + keyboard selected");
    if(bOSCBindFailed)return TEXT("Port 8000 busy: close the old OSC bridge, then retry.");
    if(ControllerOnline())return FString::Printf(TEXT("Controller connected - %s"),*OSCSender);
    return TEXT("Waiting for controller - same Wi-Fi, PC address, UDP 8000");
}
void AHallwayPuzzle::PollOSC()
{
    if(!bPhysicalControl)return;
    const double Now=FPlatformTime::Seconds();
    if(!OSCSocket&&Now>=NextOSCBind)
    {
        NextOSCBind=Now+2;
        OSCSocket=FUdpSocketBuilder(TEXT("RoomShiftNativeOSC")).AsNonBlocking().BoundToPort(8000).WithReceiveBufferSize(65536);
        bOSCBindFailed=OSCSocket==nullptr;
    }
    if(!OSCSocket)return;
    if(!ControllerOnline())ResetOSCReference();
    bool NewAngles=false,NewView=false,Shake=false;uint32 Pending=0;
    for(int32 N=0;N<128&&OSCSocket->HasPendingData(Pending);++N)
    {
        uint8 Data[2048];int32 Size=0;
        auto Sender=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if(!OSCSocket->RecvFrom(Data,sizeof(Data),Size,*Sender))break;
        FString Address;TArray<float> Values;if(!DecodeOSC(Data,Size,Address,Values))continue;
        OSCSender=Sender->ToString(false);
        if(Address==TEXT("/rotary"))
        {
            for(int32 I=0;I<5;++I)PhysicalAngles[I]=FRotator::ClampAxis(Values[I]);
            LastRotaryPacket=Now;NewAngles=true;
        }
        else
        {
            SensorYaw=Values[0];SensorPitch=Values[2];LastIMUPacket=Now;NewView=true;
            const float Acc=FMath::Abs(Values[3]);
            if(Acc<2)bShakeArmed=true;
            if(Now-FirstShakePeak>.65)ShakePeaks=0;
            if(Acc>6&&bShakeArmed&&Now-LastShake>1.5)
            {
                bShakeArmed=false;
                if(!ShakePeaks){ShakePeaks=1;FirstShakePeak=Now;}
                else if(Now-FirstShakePeak>=.07){Shake=true;ShakePeaks=0;LastShake=Now;}
            }
        }
    }
    bDeviceConnected=ControllerOnline();
    if(!bGameStarted||bMenuOpen||!ControllerOnline()){ResetOSCReference();return;}
    if(Busy())bRotaryReference=false;
    if(NewAngles&&!Busy())
    {
        if(!bRotaryReference){for(int32 I=0;I<5;++I)AngleOffsets[I]=State.Turns[State.Stage][I]*90-PhysicalAngles[I];bRotaryReference=true;}
        TArray<float> Angles;for(int32 I=0;I<5;++I)Angles.Add(PhysicalAngles[I]+AngleOffsets[I]);SetFaceAngles(Angles);
    }
    if(NewView)
    {
        if(!bViewReference)
        {
            bViewReference=true;PreviousYaw=SensorYaw;AccumulatedYaw=0;PitchReference=SensorPitch;
            ViewBaseYaw=ViewRotation.Yaw-90;ViewBasePitch=ViewRotation.Pitch;
            SmoothYaw=ViewBaseYaw;SmoothPitch=ViewBasePitch;LastViewSample=Now;
        }
        AccumulatedYaw+=FMath::FindDeltaAngleDegrees(PreviousYaw,SensorYaw);PreviousYaw=SensorYaw;
        const float TargetYaw=ViewBaseYaw+AccumulatedYaw*2.5f;
        const float TargetPitch=FMath::Clamp(ViewBasePitch+FMath::FindDeltaAngleDegrees(PitchReference,SensorPitch)*2.f,-75.f,75.f);
        const float Alpha=1.f-FMath::Exp(-float(Now-LastViewSample)/.04f);LastViewSample=Now;
        SmoothYaw+=FMath::FindDeltaAngleDegrees(SmoothYaw,TargetYaw)*Alpha;SmoothPitch=FMath::Lerp(SmoothPitch,TargetPitch,Alpha);
        SetViewAngles(SmoothYaw,SmoothPitch);
    }
    if(Shake)Assemble();
}
