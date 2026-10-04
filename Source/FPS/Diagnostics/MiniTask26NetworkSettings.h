#pragma once

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "System/MiniLogChannels.h"

#if !UE_BUILD_SHIPPING
/** Read current Driver AND all current connections; never infer settings from CLI alone. */
inline bool MiniTask26ReadNetworkSettings(UWorld* World, const TCHAR* Stage, bool bPrint, bool bRestored = false)
{
#if DO_ENABLE_NET_TEST
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!Driver) { return false; }
	int32 Lag = 0, Loss = 0;
	if (!bRestored)
	{
		FParse::Value(FCommandLine::Get(), TEXT("MiniTask26ExpectedLag="), Lag);
		FParse::Value(FCommandLine::Get(), TEXT("MiniTask26ExpectedLoss="), Loss);
	}
	const auto Check = [Lag, Loss](const FPacketSimulationSettings& S)
	{
		return S.PktLag == Lag && S.PktLoss == Loss && !S.PktLossMinSize && !S.PktLossMaxSize &&
			!S.PktLagVariance && !S.PktLagMin && !S.PktLagMax && !S.PktOrder && !S.PktDup && !S.PktJitter &&
			!S.PktIncomingLagMin && !S.PktIncomingLagMax && !S.PktIncomingLoss &&
			!S.PktFrameDelay && !S.PktIncomingFrameDelay && !S.PktBufferBloatInMS && !S.PktIncomingBufferBloatInMS;
	};
	if (!Check(Driver->PacketSimulationSettings)) { return false; }
	int32 Connections = 0;
	const auto Read = [&Check, bPrint, Stage, Lag, Loss, &Connections](const UNetConnection* Connection)
	{
		if (!Connection || !Check(Connection->PacketSimulationSettings)) { return false; }
		++Connections;
		if (bPrint) { UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net CONNECTION_READBACK: Stage=%s Connection=%s Lag=%d Loss=%d OtherFieldsZero=1 RawPingMs=%.3f InPackets=%d OutPackets=%d InLost=%d OutLost=%d"),
			Stage, *Connection->GetName(), Lag, Loss, Connection->RawPingInSeconds * 1000.0,
			Connection->InPackets, Connection->OutPackets, Connection->InPacketsLost, Connection->OutPacketsLost); }
		return true;
	};
	if (Driver->ServerConnection) { if (!Read(Driver->ServerConnection)) { return false; } }
	for (UNetConnection* Connection : Driver->ClientConnections) { if (!Read(Connection)) { return false; } }
	if (Connections < 1) { return false; }
	if (bPrint) { UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net DRIVER_READBACK: Stage=%s Role=%d World=%s Driver=%s Definition=%s Lag=%d Loss=%d Connections=%d OtherFieldsZero=1 Restored=%d"),
		Stage, int32(World->GetNetMode()), *World->GetName(), *Driver->GetName(), *Driver->GetNetDriverDefinition().ToString(), Lag, Loss, Connections, int32(bRestored)); }
	return true;
#else
	return false;
#endif
}

/** Diagnostic recovery explicitly resets the complete real Driver profile. */
inline bool MiniTask26RestoreNetworkSettings(UWorld* World)
{
#if DO_ENABLE_NET_TEST
	if (UNetDriver* Driver = World ? World->GetNetDriver() : nullptr)
	{
		FPacketSimulationSettings Baseline;
		Driver->SetPacketSimulationSettings(Baseline);
		return MiniTask26ReadNetworkSettings(World, TEXT("Restored"), true, true);
	}
#endif
	return false;
}
#endif
