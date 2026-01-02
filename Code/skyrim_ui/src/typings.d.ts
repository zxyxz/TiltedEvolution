declare const module: NodeModule;

interface NodeModule {
  id: string;
}

/** Skyim: Together type definitions */
declare namespace SkyrimTogetherTypes {
  /** Client initialization callback */
  type InitCallback = () => void;

  /** UI activation callback */
  type ActivateCallback = () => void;

  /** UI deactivation callback */
  type DeactivateCallback = () => void;

  /** Player game entry callback */
  type EnterGameCallback = () => void;

  /** Player game exit callback */
  type ExitGameCallback = () => void;

  /** Player open/close game menu callback */
  type OpeningMenuCallback = (openingMenu: boolean) => void;

  /** Chat message reception callback */
  type MessageCallback = (
    type: number,
    content: string,
    sender: string,
  ) => void;

  type CommandListEntry = {
    name: string;
    description: string;
  };

  type CommandListCallback = (commandsJson: string) => void;

  /** Connection callback */
  type ConnectCallback = () => void;

  /** Disconnection callback */
  type DisconnectCallback = (isError: boolean) => void;

  /** Name change callback */
  type SetNameCallback = (name: string) => void;

  /** Version set callback */
  type SetVersionCallback = (version: string) => void;

  /**  */
  type OnDebugCallback = (isDebug: boolean) => void;

  type UpdateDebugCallback = (
    numPacketsSent: number,
    numPacketsReceived: number,
    RTT: number,
    packetLoss: number,
    sentBandwidth: number,
    receivedBandwidth: number,
  ) => void;

  type UserDataSetCallback = (password: string, username: string) => void;

  type PlayerConnectedCallback = (
    playerId: number,
    username: string,
    level: number,
    cellName: string,
    avatar: string,
  ) => void;

  type PlayerAvatarUpdatedCallback = (playerId: number, avatar: string) => void;

  type PlayerDisconnectedCallback = (
    playerId: number,
    username: string,
  ) => void;

  type SetHealthCallback = (playerId: number, health: number) => void;

  type SetLevelCallback = (playerId: number, level: number) => void;

  type SetCellCallback = (playerId: number, cellName: string) => void;

  type SetPlayer3dLoadedCallback = (playerId: number, health: number) => void;

  type SetPlayer3dUnloadedCallback = (playerId: number) => void;

  type SetLocalPlayerIdCallback = (playerId: number) => void;

  /** Quest isolation / sync gating status callback */
  type SetSyncStatusCallback = (
    isolated: boolean,
    title: string,
    detail: string,
    moreInfo?: string,
  ) => void;

  type ProtocolMismatch = () => void;

  type TriggerError = () => void;

  type DummyDataCallback = (data: Array<number>) => void;

  type PartyInfoCallback = (playerIds: Array<number>, leaderId: number) => void;

  type PartyCreatedCallback = () => void;

  type PartyLeftCallback = (inviterId: number) => void;

  type PartyInviteReceivedCallback = (inviterId: number) => void;

  /** World-map party pins payload as JSON string */
  type SetPartyPinsCallback = (json: string) => void;

  /** Death screen shown with countdown timer */
  type ShowDeathScreenCallback = (secondsRemaining: number) => void;

  /** Death screen timer update */
  type UpdateDeathTimerCallback = (secondsRemaining: number) => void;

  /** Respawn button enabled */
  type EnableRespawnButtonCallback = () => void;

  /** Death screen hidden */
  type HideDeathScreenCallback = () => void;

  /** Revive progress for the downed player */
  type UpdateReviveVictimProgressCallback = (
    elapsedSeconds: number,
    totalSeconds: number,
    healerName: string,
  ) => void;

  /** Downed revive progress stopped/reset */
  type StopReviveVictimProgressCallback = () => void;

  /** Revive progress for the healer channeling */
  type UpdateReviveHealerProgressCallback = (
    elapsedSeconds: number,
    totalSeconds: number,
  ) => void;

  /** Healer revive overlay hidden */
  type StopReviveHealerProgressCallback = () => void;

  /** Teleport request received */
  type TeleportRequestCallback = (
    requesterId: number,
    requesterName: string,
  ) => void;

  /** Teleport countdown update */
  type TeleportCountdownCallback = (
    targetPlayerId: number,
    targetName: string,
    secondsRemaining: number,
    cancelled: boolean,
    reason: string,
  ) => void;

  /** Banner notification payload */
  type BannerCallback = (message: string, durationMs?: number) => void;

  /** Emote menu open request */
  type OpenEmoteMenuCallback = (openedFromInactive?: boolean) => void;
  /** Emote menu toggle request */
  type ToggleEmoteMenuCallback = () => void;

  type TradeInviteCallback = (inviterId: number, expiryTick: number) => void;
  type TradeInviteExpiredCallback = (inviterId: number) => void;

  interface TradeItemPayload {
    modId: number;
    baseId: number;
    count: number;
    isQuestItem: boolean;
    name: string;
    inventoryIndex?: number;
    offeredCount?: number;
  }

  interface TradeInventoryPayload extends TradeItemPayload {
    inventoryIndex: number;
    offeredCount: number;
  }

  type TradeStateUpdatedCallback = (
    active: boolean,
    partnerId: number,
    initiatedBySelf: boolean,
    selfReady: boolean,
    partnerReady: boolean,
    selfItems: TradeItemPayload[],
    partnerItems: TradeItemPayload[],
    inventory: TradeInventoryPayload[],
  ) => void;

  type TradeCancelledCallback = (
    partnerId: number,
    reason: number,
    wasInitiator: boolean,
  ) => void;

  type TradeCompletedCallback = (partnerId: number) => void;

  type TradeOfferEntry = { index: number; count: number };
}

/** Global Skyrim: Together object. */
declare const skyrimtogether: SkyrimTogether;

/** Global Skyrim: Together object type. */
interface SkyrimTogether {
  /** Add listener to when the UI is first initialized. */
  on(event: 'init', callback: SkyrimTogetherTypes.InitCallback): void;

  /** Add listener to when the UI is activated. */
  on(event: 'activate', callback: SkyrimTogetherTypes.ActivateCallback): void;

  /** Add listener to when the UI is deactivated. */
  on(
    event: 'deactivate',
    callback: SkyrimTogetherTypes.DeactivateCallback,
  ): void;

  /** Add listener to when the player enters a game. */
  on(event: 'enterGame', callback: SkyrimTogetherTypes.EnterGameCallback): void;

  /** Add listener to when the player exits a game. */
  on(event: 'exitGame', callback: SkyrimTogetherTypes.ExitGameCallback): void;

  /** Add listener to when the player open/close a game menu. */
  on(
    event: 'openingMenu',
    callback: SkyrimTogetherTypes.OpeningMenuCallback,
  ): void;

  /** Add listener to when a player message is received. */
  on(event: 'message', callback: SkyrimTogetherTypes.MessageCallback): void;

  /** Add listener to when the server command list is updated. */
  on(
    event: 'commandList',
    callback: SkyrimTogetherTypes.CommandListCallback,
  ): void;

  /** Add listener to when the player connects to a server. */
  on(event: 'connect', callback: SkyrimTogetherTypes.ConnectCallback): void;

  /** Add listener to when the player disconnects from a server. */
  on(
    event: 'disconnect',
    callback: SkyrimTogetherTypes.DisconnectCallback,
  ): void;

  /** Add listener to when the player's name changes. */
  on(event: 'setName', callback: SkyrimTogetherTypes.SetNameCallback): void;

  /** Add listener to when the client's version is set. */
  on(
    event: 'setVersion',
    callback: SkyrimTogetherTypes.SetVersionCallback,
  ): void;

  /** Add listener to when the player's press the F3 key */
  on(event: 'debug', callback: SkyrimTogetherTypes.OnDebugCallback): void;

  on(
    event: 'debugData',
    callback: SkyrimTogetherTypes.UpdateDebugCallback,
  ): void;

  /** Add listener to when the player's is connected with the launcher */
  on(
    event: 'userDataSet',
    callback: SkyrimTogetherTypes.UserDataSetCallback,
  ): void;

  /** Add listener to when one player connect in server. */
  on(
    event: 'playerConnected',
    callback: SkyrimTogetherTypes.PlayerConnectedCallback,
  ): void;

  /** Add listener to when one player disconnect in server. */
  on(
    event: 'playerDisconnected',
    callback: SkyrimTogetherTypes.PlayerDisconnectedCallback,
  ): void;

  on(
    event: 'playerAvatarUpdated',
    callback: SkyrimTogetherTypes.PlayerAvatarUpdatedCallback,
  ): void;

  on(event: 'setHealth', callback: SkyrimTogetherTypes.SetHealthCallback): void;

  /** Add listener to when one player change level in server. */
  on(event: 'setLevel', callback: SkyrimTogetherTypes.SetLevelCallback): void;

  /** Add listener to when one player change cell in server. */
  on(event: 'setCell', callback: SkyrimTogetherTypes.SetCellCallback): void;

  /** Add listener to when a player is loaded or unloaded in 3D.  */
  on(
    event: 'setPlayer3dLoaded',
    callback: SkyrimTogetherTypes.SetPlayer3dLoadedCallback,
  ): void;

  on(
    event: 'setPlayer3dUnloaded',
    callback: SkyrimTogetherTypes.SetPlayer3dUnloadedCallback,
  ): void;

  on(
    event: 'setLocalPlayerId',
    callback: SkyrimTogetherTypes.SetLocalPlayerIdCallback,
  ): void;

  on(
    event: 'setSyncStatus',
    callback: SkyrimTogetherTypes.SetSyncStatusCallback,
  ): void;

  on(
    event: 'protocolMismatch',
    callback: SkyrimTogetherTypes.ProtocolMismatch,
  ): void;

  on(event: 'triggerError', callback: SkyrimTogetherTypes.TriggerError): void;

  on(event: 'dummyData', callback: SkyrimTogetherTypes.DummyDataCallback): void;

  on(event: 'partyInfo', callback: SkyrimTogetherTypes.PartyInfoCallback): void;

  on(
    event: 'partyCreated',
    callback: SkyrimTogetherTypes.PartyCreatedCallback,
  ): void;

  on(event: 'partyLeft', callback: SkyrimTogetherTypes.PartyLeftCallback): void;

  on(
    event: 'partyInviteReceived',
    callback: SkyrimTogetherTypes.PartyInviteReceivedCallback,
  ): void;
  on(
    event: 'tradeInviteReceived',
    callback: SkyrimTogetherTypes.TradeInviteCallback,
  ): void;
  on(
    event: 'tradeInviteExpired',
    callback: SkyrimTogetherTypes.TradeInviteExpiredCallback,
  ): void;
  on(
    event: 'tradeStateUpdated',
    callback: SkyrimTogetherTypes.TradeStateUpdatedCallback,
  ): void;
  on(
    event: 'tradeCancelled',
    callback: SkyrimTogetherTypes.TradeCancelledCallback,
  ): void;
  on(
    event: 'tradeCompleted',
    callback: SkyrimTogetherTypes.TradeCompletedCallback,
  ): void;

  on(
    event: 'teleportRequest',
    callback: SkyrimTogetherTypes.TeleportRequestCallback,
  ): void;

  on(
    event: 'teleportCountdown',
    callback: SkyrimTogetherTypes.TeleportCountdownCallback,
  ): void;

  /** Add listener to open the emote menu from native input. */
  on(
    event: 'openEmoteMenu',
    callback: SkyrimTogetherTypes.OpenEmoteMenuCallback,
  ): void;
  /** Add listener to toggle the emote menu from native input. */
  on(
    event: 'toggleEmoteMenu',
    callback: SkyrimTogetherTypes.ToggleEmoteMenuCallback,
  ): void;

  /** Add listener to transient overlay banners. */
  on(event: 'showBanner', callback: SkyrimTogetherTypes.BannerCallback): void;

  /** Add listener to when the death screen is shown. */
  on(
    event: 'showDeathScreen',
    callback: SkyrimTogetherTypes.ShowDeathScreenCallback,
  ): void;

  /** Add listener to when the death screen timer updates. */
  on(
    event: 'updateDeathTimer',
    callback: SkyrimTogetherTypes.UpdateDeathTimerCallback,
  ): void;

  /** Add listener to when the respawn button is enabled. */
  on(
    event: 'enableRespawnButton',
    callback: SkyrimTogetherTypes.EnableRespawnButtonCallback,
  ): void;

  /** Add listener to when the death screen is hidden. */
  on(
    event: 'hideDeathScreen',
    callback: SkyrimTogetherTypes.HideDeathScreenCallback,
  ): void;

  on(
    event: 'updateReviveVictimProgress',
    callback: SkyrimTogetherTypes.UpdateReviveVictimProgressCallback,
  ): void;

  on(
    event: 'stopReviveVictimProgress',
    callback: SkyrimTogetherTypes.StopReviveVictimProgressCallback,
  ): void;

  on(
    event: 'updateReviveHealerProgress',
    callback: SkyrimTogetherTypes.UpdateReviveHealerProgressCallback,
  ): void;

  on(
    event: 'stopReviveHealerProgress',
    callback: SkyrimTogetherTypes.StopReviveHealerProgressCallback,
  ): void;

  /** Remove listener from when the application is first initialized. */
  off(event: 'init', callback?: SkyrimTogetherTypes.InitCallback): void;

  /** Remove listener from when the UI is activated. */
  off(event: 'activate', callback?: SkyrimTogetherTypes.ActivateCallback): void;

  /** Remove listener from when the UI is deactivated. */
  off(
    event: 'deactivate',
    callback?: SkyrimTogetherTypes.DeactivateCallback,
  ): void;

  /** Remove listener from when the player enters a game. */
  off(
    event: 'enterGame',
    callback?: SkyrimTogetherTypes.EnterGameCallback,
  ): void;

  /** Remove listener from when the player exits a game. */
  off(event: 'exitGame', callback?: SkyrimTogetherTypes.ExitGameCallback): void;

  /** Add listener to when the player open/close a game menu. */
  off(
    event: 'openingMenu',
    callback?: SkyrimTogetherTypes.OpeningMenuCallback,
  ): void;

  /** Remove listener from when a player message is received. */
  off(event: 'message', callback?: SkyrimTogetherTypes.MessageCallback): void;

  /** Remove listener from when the player connects to a server. */
  off(event: 'connect', callback?: SkyrimTogetherTypes.ConnectCallback): void;

  /** Remove listener from when the player disconnects from a server. */
  off(
    event: 'disconnect',
    callback?: SkyrimTogetherTypes.DisconnectCallback,
  ): void;

  /** Remove listener from when the player's name changes. */
  off(event: 'setName', callback?: SkyrimTogetherTypes.SetNameCallback): void;

  /** Remove listener from when the client's version is set. */
  off(
    event: 'setVersion',
    callback?: SkyrimTogetherTypes.SetVersionCallback,
  ): void;

  /** Remove listener to when the player's press the F3 key */
  off(event: 'debug', callback?: SkyrimTogetherTypes.OnDebugCallback): void;

  off(
    event: 'debugData',
    callback?: SkyrimTogetherTypes.UpdateDebugCallback,
  ): void;

  /** Add listener to when one player connect in server. */
  off(
    event: 'playerConnected',
    callback?: SkyrimTogetherTypes.PlayerConnectedCallback,
  ): void;

  /** Add listener to when one player disconnect in server. */
  off(
    event: 'playerDisconnected',
    callback?: SkyrimTogetherTypes.PlayerDisconnectedCallback,
  ): void;

  off(
    event: 'playerAvatarUpdated',
    callback?: SkyrimTogetherTypes.PlayerAvatarUpdatedCallback,
  ): void;

  off(
    event: 'userDataSet',
    callback?: SkyrimTogetherTypes.UserDataSetCallback,
  ): void;

  off(
    event: 'setHealth',
    callback?: SkyrimTogetherTypes.SetHealthCallback,
  ): void;

  off(event: 'setLevel', callback?: SkyrimTogetherTypes.SetLevelCallback): void;

  off(event: 'setCell', callback?: SkyrimTogetherTypes.SetCellCallback): void;

  /** Add listener to when a player is loaded or unloaded in 3D.  */
  off(
    event: 'setPlayer3dLoaded',
    callback?: SkyrimTogetherTypes.SetPlayer3dLoadedCallback,
  ): void;

  off(
    event: 'setPlayer3dUnloaded',
    callback?: SkyrimTogetherTypes.SetPlayer3dUnloadedCallback,
  ): void;

  off(
    event: 'setLocalPlayerId',
    callback?: SkyrimTogetherTypes.SetLocalPlayerIdCallback,
  ): void;

  off(
    event: 'setSyncStatus',
    callback?: SkyrimTogetherTypes.SetSyncStatusCallback,
  ): void;

  off(
    event: 'protocolMismatch',
    callback?: SkyrimTogetherTypes.ProtocolMismatch,
  ): void;

  off(event: 'triggerError', callback?: SkyrimTogetherTypes.TriggerError): void;

  off(
    event: 'dummyData',
    callback?: SkyrimTogetherTypes.DummyDataCallback,
  ): void;

  off(
    event: 'partyInfo',
    callback?: SkyrimTogetherTypes.PartyInfoCallback,
  ): void;

  off(
    event: 'partyCreated',
    callback?: SkyrimTogetherTypes.PartyCreatedCallback,
  ): void;

  off(
    event: 'partyLeft',
    callback?: SkyrimTogetherTypes.PartyLeftCallback,
  ): void;

  off(
    event: 'partyInviteReceived',
    callback?: SkyrimTogetherTypes.PartyInviteReceivedCallback,
  ): void;
  off(
    event: 'tradeInviteReceived',
    callback?: SkyrimTogetherTypes.TradeInviteCallback,
  ): void;
  off(
    event: 'tradeInviteExpired',
    callback?: SkyrimTogetherTypes.TradeInviteExpiredCallback,
  ): void;
  off(
    event: 'tradeStateUpdated',
    callback?: SkyrimTogetherTypes.TradeStateUpdatedCallback,
  ): void;
  off(
    event: 'tradeCancelled',
    callback?: SkyrimTogetherTypes.TradeCancelledCallback,
  ): void;
  off(
    event: 'tradeCompleted',
    callback?: SkyrimTogetherTypes.TradeCompletedCallback,
  ): void;

  off(
    event: 'teleportRequest',
    callback?: SkyrimTogetherTypes.TeleportRequestCallback,
  ): void;

  off(
    event: 'teleportCountdown',
    callback?: SkyrimTogetherTypes.TeleportCountdownCallback,
  ): void;

  /** Remove listener from emote menu open requests. */
  off(
    event: 'openEmoteMenu',
    callback?: SkyrimTogetherTypes.OpenEmoteMenuCallback,
  ): void;
  /** Remove listener from emote menu toggle requests. */
  off(
    event: 'toggleEmoteMenu',
    callback?: SkyrimTogetherTypes.ToggleEmoteMenuCallback,
  ): void;

  /** Remove listener from overlay banners. */
  off(event: 'showBanner', callback?: SkyrimTogetherTypes.BannerCallback): void;

  /** Remove listener from when the death screen is shown. */
  off(
    event: 'showDeathScreen',
    callback?: SkyrimTogetherTypes.ShowDeathScreenCallback,
  ): void;

  /** Remove listener from when the death screen timer updates. */
  off(
    event: 'updateDeathTimer',
    callback?: SkyrimTogetherTypes.UpdateDeathTimerCallback,
  ): void;

  /** Remove listener from when the respawn button is enabled. */
  off(
    event: 'enableRespawnButton',
    callback?: SkyrimTogetherTypes.EnableRespawnButtonCallback,
  ): void;

  /** Remove listener from when the death screen is hidden. */
  off(
    event: 'hideDeathScreen',
    callback?: SkyrimTogetherTypes.HideDeathScreenCallback,
  ): void;

  off(
    event: 'updateReviveVictimProgress',
    callback?: SkyrimTogetherTypes.UpdateReviveVictimProgressCallback,
  ): void;

  off(
    event: 'stopReviveVictimProgress',
    callback?: SkyrimTogetherTypes.StopReviveVictimProgressCallback,
  ): void;

  off(
    event: 'updateReviveHealerProgress',
    callback?: SkyrimTogetherTypes.UpdateReviveHealerProgressCallback,
  ): void;

  off(
    event: 'stopReviveHealerProgress',
    callback?: SkyrimTogetherTypes.StopReviveHealerProgressCallback,
  ): void;

  /**
   * Connect to server at given address and port.
   *
   * @param host IP address or hostname.
   * @param port Port.
   * @param username Account username.
   * @param password Account password.
   * @param serverPassword Optional legacy server password.
   */
  connect(
    host: string,
    port: number,
    username: string,
    password: string,
    serverPassword?: string,
  ): void;

  /**
   * Disconnect from server or cancel connection.
   */
  disconnect(): void;

  /**
   * Reveal other players in the immediate area.
   */
  revealPlayers(): void;

  /**
   * Trigger a pre-defined emote animation on the local player.
   *
   * @param eventName Animation graph event to fire.
   */
  playEmote(eventName: string): void;

  /**
   * Send message to server.
   */
  sendMessage(type: number, message: string): void;

  /**
   * Send a request to the server for changing the in-game time.
   */
  setTime(hours: number, minutes: number): void;

  /**
   * Deactivate UI and release control.
   */
  deactivate(): void;

  /**
   * Request teleportation to a given player.
   *
   * @param playerId Id of the player to whom the request should be sent.
   */
  teleportToPlayer(playerId: number): void;

  /**
   * Respond to an incoming teleport request.
   *
   * @param requesterId Id of the player that issued the request.
   * @param accepted Whether the request is accepted.
   */
  respondTeleportRequest(requesterId: number, accepted: boolean): void;

  /**
   * Reconnect the client.
   */
  reconnect(): void;

  /**
   * Launch a party.
   */
  launchParty(): void;

  /**
   * Send a party invite to player with player id.
   *
   * @param playerId Id of the player to which the invite should be sent.
   */
  createPartyInvite(playerId: number): void;

  /**
   * Accept a party invite.
   *
   * @param inviterId Id of the player who sent the invite.
   */
  acceptPartyInvite(inviterId: number): void;

  /**
   * As a party leader, kick a member from the party.
   *
   * @param playerId Id of the player who gets kicked.
   */
  kickPartyMember(playerId: number): void;

  /**
   * Leave the currently joined party.
   */
  leaveParty(): void;

  /**
   * As a party leader, make someone else the leader.
   *
   * @param playerId Id of the new leader.
   */
  changePartyLeader(playerId: number): void;

  /**
   * Send a trade invite to another player.
   *
   * @param playerId Id of the player to trade with.
   */
  sendTradeInvite(playerId: number): void;

  /**
   * Respond to a trade invite.
   *
   * @param playerId Id of the inviter.
   * @param accept Whether to accept the invitation.
   */
  respondTradeInvite(playerId: number, accept: boolean): void;

  /**
   * Cancel the current trade session or pending invite.
   */
  cancelTrade(): void;

  /**
   * Update the local ready state for the current trade session.
   *
   * @param ready Whether the player is ready to finalize the trade.
   */
  setTradeReady(ready: boolean): void;

  /**
   * Update the items offered in the current trade session.
   *
   * @param entries Selection of inventory indices and counts to offer.
   */
  updateTradeOffer(entries: SkyrimTogetherTypes.TradeOfferEntry[]): void;

  /**
   * Upload or clear the local profile picture shown to party members.
   *
   * @param imageData Data URL (e.g. "data:image/png;base64,...") or empty string to clear.
   */
  setProfilePicture(imageData: string): void;

  /**
   * Select how player name tags are rendered in the world.
   *
   * @param mode Numeric representation of the desired nametag display mode.
   */
  setNameTagMode(mode: number): void;

  /**
   * Called when the player clicks the respawn button on the death screen.
   */
  respawnButtonClicked(): void;
}
