import { Injectable, NgZone, OnDestroy } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { AsyncSubject, BehaviorSubject, ReplaySubject, Subject } from 'rxjs';
import { environment } from '../../environments/environment';
import { Debug } from '../models/debug';
import { PartyInfo } from '../models/party-info';
import { Player } from '../models/player';
import { View } from '../models/view.enum';
import { ChatService } from './chat.service';
import { ErrorEvents, ErrorService } from './error.service';
import { LoadingService } from './loading.service';
import { PartyPin } from '../models/party-pin';
import { UiRepository } from '../store/ui.repository';
import { OverlayBannerService } from './overlay-banner.service';

export interface TradeItemPayload {
  modId: number;
  baseId: number;
  count: number;
  isQuestItem: boolean;
  name: string;
  inventoryIndex?: number;
  offeredCount?: number;
  isGold?: boolean;
  details?: string[];
  raw?: any;
}

export interface TradeStatePayload {
  active: boolean;
  partnerId: number;
  initiatedBySelf: boolean;
  selfReady: boolean;
  partnerReady: boolean;
  selfItems: TradeItemPayload[];
  partnerItems: TradeItemPayload[];
  inventory: TradeItemPayload[];
  countdownMs: number;
  countdownTotalMs: number;
}

export interface TradeInvitePayload {
  inviterId: number;
  expiryTick: number;
}

export interface TradeCancellationPayload {
  partnerId: number;
  reason: number;
  wasInitiator: boolean;
}

export interface ReviveProgressPayload {
  elapsedSeconds: number;
  totalSeconds: number;
  label?: string;
}

export interface SyncStatusPayload {
  isolated: boolean;
  title: string;
  detail: string;
  moreInfo: string;
}

/** Client game service. */
@Injectable({
  providedIn: 'root',
})
export class ClientService implements OnDestroy {
  /** Initialization done. */
  public initDone = new AsyncSubject<undefined>();

  /** Activation state change. */
  public activationStateChange = new BehaviorSubject(false);

  /** InGame state change. */
  public inGameStateChange = new BehaviorSubject(!environment.game);

  /** Opening/close menu change. */
  public openingMenuChange = new BehaviorSubject(false);

  /** Connection state change. */
  public connectionStateChange = new BehaviorSubject(false);

  public isConnectionInProgressChange = new BehaviorSubject(false);

  /** Quest isolation / sync gating status. */
  public syncStatusChange = new BehaviorSubject<SyncStatusPayload>({
    isolated: false,
    title: '',
    detail: '',
    moreInfo: '',
  });

  /** Player name change. */
  public nameChange = new BehaviorSubject(environment.game ? '' : 'test');

  /** Client version setting. */
  public versionSet = new BehaviorSubject(environment.game ? '' : 'browser');

  /** Debug state change. */
  public debugStateChange = new Subject<boolean>();

  /** Debug data change. */
  public debugDataChange = new BehaviorSubject(new Debug());

  /** Connect player to server change. */
  public playerConnectedChange = new Subject<Player>();

  /** Connect party info change. */
  public partyInfoChange = new Subject<PartyInfo>();

  /** Connect party info change. */
  public partyLeftChange = new Subject<void>();

  /** Party pins for world map overlay. */
  public partyPinsChange = new BehaviorSubject<PartyPin[]>([]);

  /** Connect party invite received. */
  public partyInviteReceivedChange = new Subject<number>();

  /** Trade invite received. */
  public tradeInviteChange = new Subject<TradeInvitePayload>();

  /** Trade invite expired. */
  public tradeInviteExpiredChange = new Subject<number>();

  /** Active trade state change. */
  public tradeStateChange = new BehaviorSubject<TradeStatePayload | undefined>(
    undefined,
  );

  /** Trade cancellation notifications. */
  public tradeCancelledChange = new Subject<TradeCancellationPayload>();

  /** Trade completion notifications. */
  public tradeCompletedChange = new Subject<number>();

  /** Player avatar update. */
  public avatarChange = new Subject<Player>();

  /** Disconnect player to server change. */
  public playerDisconnectedChange = new Subject<Player>();

  /** Player health change. */
  public healthChange = new Subject<Player>();

  /** Player level change. */
  public levelChange = new Subject<Player>();

  /** Player cell change. */
  public cellChange = new Subject<Player>();

  /** Player isLoaded change. */
  public isLoadedChange = new Subject<Player>();

  public protocolMismatchChange = new BehaviorSubject(false);

  /** Receive error from core */
  public triggerError = new ReplaySubject<ErrorEvents>(1);

  /** Used purely for debugging. */
  public debugChange = new Subject<void>();

  /** Teleport request received. */
  public teleportRequestChange = new Subject<{
    requesterId: number;
    requesterName: string;
  }>();

  /** Teleport request handled. */
  public teleportRequestHandledChange = new Subject<{
    requesterId: number;
    accepted: boolean;
  }>();

  // The below emitters are used in the mocking service

  /** Used for when a party leader changed. */
  public partyLaunchedChange = new Subject<void>();

  /** Used for when a party invite is sent. */
  public partyInviteChange = new Subject<number>();

  /** Used for when a party invite was accepted by the local player. */
  public partyJoinedChange = new Subject<number>();

  /** Used for when a party member was kicked. */
  public memberKickedChange = new Subject<number>();

  /** Used for when a party leader changed. */
  public partyLeaderChange = new Subject<number>();

  /** Death screen shown with countdown. */
  public deathScreenChange = new Subject<number>();

  /** Death screen timer update. */
  public deathTimerChange = new Subject<number>();

  /** Respawn button enabled. */
  public respawnButtonEnabledChange = new Subject<void>();

  /** Death screen hidden. */
  public deathScreenHiddenChange = new Subject<void>();

  /** Revive progress updates for the downed player. */
  public reviveVictimProgressChange = new BehaviorSubject<
    ReviveProgressPayload | undefined
  >(undefined);

  /** Revive progress updates for the healer. */
  public reviveHealerProgressChange = new BehaviorSubject<
    ReviveProgressPayload | undefined
  >(undefined);

  public localPlayerId = undefined;
  public localPlayerIdChange = new BehaviorSubject<number | undefined>(
    undefined,
  );

  private _host = '';

  private _port = 0;

  private _username = '';

  private _password = '';

  private _serverPassword = '';

  private _remainingReconnectionAttempt = environment.nbReconnectionAttempts;

  /**
   * Instantiate.
   */
  public constructor(
    private readonly zone: NgZone,
    private readonly errorService: ErrorService,
    private readonly loadingService: LoadingService,
    private readonly translocoService: TranslocoService,
    private readonly chatService: ChatService,
    private readonly uiRepository: UiRepository,
    private readonly overlayBannerService: OverlayBannerService,
  ) {
    skyrimtogether.on('init', this.onInit.bind(this));
    skyrimtogether.on('activate', this.onActivate.bind(this));
    skyrimtogether.on('deactivate', this.onDeactivate.bind(this));
    skyrimtogether.on('enterGame', this.onEnterGame.bind(this));
    skyrimtogether.on('exitGame', this.onExitGame.bind(this));
    skyrimtogether.on('openingMenu', this.onOpeningMenu.bind(this));
    skyrimtogether.on('connect', this.onConnect.bind(this));
    skyrimtogether.on('disconnect', this.onDisconnect.bind(this));
    skyrimtogether.on('setName', this.onSetName.bind(this)); //not wanted, we dont sync name changes
    skyrimtogether.on('setVersion', this.onSetVersion.bind(this));
    skyrimtogether.on('debug', this.onDebug.bind(this)); //not needed anymore
    skyrimtogether.on('debugData', this.onUpdateDebug.bind(this));
    skyrimtogether.on('playerConnected', this.onPlayerConnected.bind(this));
    skyrimtogether.on(
      'playerDisconnected',
      this.onPlayerDisconnected.bind(this),
    );
    skyrimtogether.on('playerAvatarUpdated', this.onPlayerAvatar.bind(this));
    skyrimtogether.on('setHealth', this.onSetHealth.bind(this));
    skyrimtogether.on('setLevel', this.onSetLevel.bind(this));
    skyrimtogether.on('setCell', this.onSetCell.bind(this));
    skyrimtogether.on('setPlayer3dLoaded', this.onSetPlayer3dLoaded.bind(this));
    skyrimtogether.on(
      'setPlayer3dUnloaded',
      this.onSetPlayer3dUnloaded.bind(this),
    );
    skyrimtogether.on('setLocalPlayerId', this.onSetLocalPlayerId.bind(this));
    (skyrimtogether as any).on(
      'setSyncStatus',
      this.onSetSyncStatus.bind(this),
    );
    skyrimtogether.on('protocolMismatch', this.onProtocolMismatch.bind(this));
    skyrimtogether.on('triggerError', this.onTriggerError.bind(this));
    skyrimtogether.on('dummyData', this.onDummyData.bind(this));
    skyrimtogether.on('partyInfo', this.onPartyInfo.bind(this));
    skyrimtogether.on('teleportRequest', this.onTeleportRequest.bind(this));
    skyrimtogether.on('teleportCountdown', this.onTeleportCountdown.bind(this));
    skyrimtogether.on('partyCreated', this.onPartyCreated.bind(this));
    skyrimtogether.on('partyLeft', this.onPartyLeft.bind(this));
    skyrimtogether.on(
      'partyInviteReceived',
      this.onPartyInviteReceived.bind(this),
    );
    skyrimtogether.on(
      'tradeInviteReceived',
      this.onTradeInviteReceived.bind(this),
    );
    skyrimtogether.on(
      'tradeInviteExpired',
      this.onTradeInviteExpired.bind(this),
    );
    skyrimtogether.on('tradeStateUpdated', this.onTradeStateUpdated.bind(this));
    skyrimtogether.on('tradeCancelled', this.onTradeCancelled.bind(this));
    skyrimtogether.on('tradeCompleted', this.onTradeCompleted.bind(this));
    (skyrimtogether as any).on('setPartyPins', this.onSetPartyPins.bind(this));
    skyrimtogether.on('showDeathScreen', this.onShowDeathScreen.bind(this));
    skyrimtogether.on('updateDeathTimer', this.onUpdateDeathTimer.bind(this));
    skyrimtogether.on(
      'enableRespawnButton',
      this.onEnableRespawnButton.bind(this),
    );
    skyrimtogether.on('hideDeathScreen', this.onHideDeathScreen.bind(this));
    skyrimtogether.on(
      'updateReviveVictimProgress',
      this.onUpdateReviveVictimProgress.bind(this),
    );
    skyrimtogether.on(
      'stopReviveVictimProgress',
      this.onStopReviveVictimProgress.bind(this),
    );
    skyrimtogether.on(
      'updateReviveHealerProgress',
      this.onUpdateReviveHealerProgress.bind(this),
    );
    skyrimtogether.on(
      'stopReviveHealerProgress',
      this.onStopReviveHealerProgress.bind(this),
    );
    skyrimtogether.on('showBanner', this.onShowBanner.bind(this));
    skyrimtogether.on('openEmoteMenu', this.onOpenEmoteMenu.bind(this));
    (skyrimtogether as any).on(
      'toggleEmoteMenu',
      this.onToggleEmoteMenu.bind(this),
    );
  }

  /**
   * Dispose.
   */
  public ngOnDestroy(): void {
    skyrimtogether.off('init');
    skyrimtogether.off('activate');
    skyrimtogether.off('deactivate');
    skyrimtogether.off('enterGame');
    skyrimtogether.off('exitGame');
    skyrimtogether.off('openingMenu');
    skyrimtogether.off('connect');
    skyrimtogether.off('disconnect');
    skyrimtogether.off('setName');
    skyrimtogether.off('setVersion');
    skyrimtogether.off('debug');
    skyrimtogether.off('debugData');
    skyrimtogether.off('playerConnected');
    skyrimtogether.off('playerDisconnected');
    skyrimtogether.off('playerAvatarUpdated');
    skyrimtogether.off('setHealth');
    skyrimtogether.off('setLevel');
    skyrimtogether.off('setCell');
    skyrimtogether.off('setPlayer3dLoaded');
    skyrimtogether.off('setPlayer3dUnloaded');
    skyrimtogether.off('setLocalPlayerId');
    skyrimtogether.off('setSyncStatus');
    skyrimtogether.off('protocolMismatch');
    skyrimtogether.off('triggerError');
    skyrimtogether.off('dummyData');
    skyrimtogether.off('partyInfo');
    skyrimtogether.off('teleportRequest');
    skyrimtogether.off('teleportCountdown');
    skyrimtogether.off('partyCreated');
    skyrimtogether.off('partyLeft');
    skyrimtogether.off('partyInviteReceived');
    skyrimtogether.off('tradeInviteReceived');
    skyrimtogether.off('tradeInviteExpired');
    skyrimtogether.off('tradeStateUpdated');
    skyrimtogether.off('tradeCancelled');
    skyrimtogether.off('tradeCompleted');
    (skyrimtogether as any).off('setPartyPins');
    skyrimtogether.off('showBanner');
    skyrimtogether.off('openEmoteMenu');
    (skyrimtogether as any).off('toggleEmoteMenu');
    skyrimtogether.off('showDeathScreen');
    skyrimtogether.off('updateDeathTimer');
    skyrimtogether.off('enableRespawnButton');
    skyrimtogether.off('hideDeathScreen');
    skyrimtogether.off('updateReviveVictimProgress');
    skyrimtogether.off('stopReviveVictimProgress');
    skyrimtogether.off('updateReviveHealerProgress');
    skyrimtogether.off('stopReviveHealerProgress');
  }

  /**
   * Connect to a server at the given address and port.
   *
   * @param host IP address or hostname.
   * @param port Port.
   * @param username Account username used for server-side authentication.
   * @param password Account password used for server-side authentication.
   * @param serverPassword Optional legacy server password/admin token.
   */
  public connect(
    host: string,
    port: number,
    username: string,
    password: string,
    serverPassword = '',
  ): void {
    if (serverPassword && serverPassword.length > 0) {
      skyrimtogether.connect(host, port, username, password, serverPassword);
    } else {
      skyrimtogether.connect(host, port, username, password);
    }
    this.isConnectionInProgressChange.next(true);
    this._host = host;
    this._port = port;
    this._username = username;
    this._password = password;
    this._serverPassword = serverPassword;

    if (username && username.length > 0) {
      this.zone.run(() => {
        this.nameChange.next(username);
      });
    }
  }

  /**
   * Disconnect from the server or cancel connection.
   */
  public disconnect(): void {
    skyrimtogether.disconnect();
    this._remainingReconnectionAttempt = 0;
  }

  /**
   * Reveal players in the immediate area (lighting them up with a visual effect).
   */
  public revealPlayers(): void {
    skyrimtogether.revealPlayers();
  }

  /**
   * Trigger an emote animation locally (and sync it to other players).
   */
  public playEmote(eventName: string): void {
    if (!eventName) {
      return;
    }
    skyrimtogether.playEmote(eventName);
  }

  /**
   * Launch a party.
   */
  public launchParty(): void {
    skyrimtogether.launchParty();
  }

  /**
   * Create a party invite.
   */
  public createPartyInvite(playerId: number): void {
    skyrimtogether.createPartyInvite(playerId);
  }

  /**
   * Accept a party invite.
   */
  public acceptPartyInvite(inviterId: number): void {
    skyrimtogether.acceptPartyInvite(inviterId);
  }

  /**
   * As a party leader, kick a player from the party.
   */
  public kickPartyMember(playerId: number): void {
    skyrimtogether.kickPartyMember(playerId);
  }

  /**
   * Leave a party.
   */
  public leaveParty(): void {
    skyrimtogether.leaveParty();
  }

  /**
   * As a party leader, make someone else the leader.
   */
  public changePartyLeader(playerId: number): void {
    skyrimtogether.changePartyLeader(playerId);
  }

  /** Send a trade invite to another player. */
  public sendTradeInvite(playerId: number): void {
    skyrimtogether.sendTradeInvite(playerId);
  }

  /** Respond to an incoming trade invite. */
  public respondTradeInvite(playerId: number, accept: boolean): void {
    skyrimtogether.respondTradeInvite(playerId, accept);
  }

  /** Cancel the current trade session or outstanding invite. */
  public cancelTrade(): void {
    skyrimtogether.cancelTrade();
  }

  /** Toggle readiness state in the current trade session. */
  public setTradeReady(ready: boolean): void {
    skyrimtogether.setTradeReady(ready);
  }

  /** Update the offered items for the current trade session. */
  public updateTradeOffer(entries: { index: number; count: number }[]): void {
    skyrimtogether.updateTradeOffer(entries);
  }

  /** Upload or clear the local profile picture. */
  public setProfilePicture(imageData: string): void {
    skyrimtogether.setProfilePicture(imageData);
  }

  /**
   * Deactivate UI and release control.
   */
  public deactivate(): void {
    skyrimtogether.deactivate();
  }

  /**
   * Reconnect
   */
  public reconnect(): void {
    skyrimtogether.reconnect();
    this._remainingReconnectionAttempt = 0;
  }

  /**
   * Request teleportation to another player.
   *
   * @param playerId Target player identifier.
   */
  public requestTeleport(playerId: number): void {
    skyrimtogether.teleportToPlayer(playerId);
  }

  /**
   * Respond to an incoming teleport request.
   *
   * @param requesterId Requesting player's identifier.
   * @param accepted Whether the request is accepted.
   */
  public respondTeleportRequest(requesterId: number, accepted: boolean): void {
    skyrimtogether.respondTeleportRequest(requesterId, accepted);
    this.zone.run(() => {
      this.teleportRequestHandledChange.next({ requesterId, accepted });
    });
  }

  public teleportToPlayer(playerId: number): void {
    this.requestTeleport(playerId);
  }

  /**
   * Called when the UI is first initialized.
   */
  // TODO: is this still used?
  private onInit(): void {
    this.zone.run(() => {
      this.initDone.next(undefined);
      this.initDone.complete();
    });
  }

  /**
   * Called when the UI is activated.
   */
  private onActivate(): void {
    this.zone.run(() => {
      this.activationStateChange.next(true);
    });
  }

  /**
   * Called when the UI is deactivated.
   */
  private onDeactivate(): void {
    this.zone.run(() => {
      this.activationStateChange.next(false);
    });
  }

  /**
   * Called when the player enters a game.
   */
  private onEnterGame(): void {
    this.zone.run(() => {
      this.inGameStateChange.next(true);
    });
  }

  /**
   * Called when the player exits a game.
   */
  private onExitGame(): void {
    this.zone.run(() => {
      this.inGameStateChange.next(false);
    });
  }

  /**
   * Called when the player open/close a game menu
   * @param openingMenu
   */
  private onOpeningMenu(openingMenu: boolean): void {
    this.zone.run(() => {
      this.openingMenuChange.next(openingMenu);
    });
  }

  /**
   * Called when a connection is made.
   */
  private onConnect(): void {
    this.zone.run(async () => {
      this._remainingReconnectionAttempt = environment.nbReconnectionAttempts;
      this.isConnectionInProgressChange.next(false);
      this.connectionStateChange.next(true);

      this.chatService.pushSystemMessage('SERVICE.CLIENT.CONNECTED');
    });
  }

  /**
   * Called when a connection is terminated.
   */
  private onDisconnect(isError: boolean): void {
    void this.zone.run(async () => {
      this.localPlayerId = undefined;
      this.localPlayerIdChange.next(undefined);
      this.connectionStateChange.next(false);
      this.isConnectionInProgressChange.next(false);
      this.syncStatusChange.next({
        isolated: false,
        title: '',
        detail: '',
        moreInfo: '',
      });

      if (isError && this._remainingReconnectionAttempt > 0) {
        this._remainingReconnectionAttempt--;
        this.chatService.pushSystemMessage('SERVICE.CLIENT.CONNECTION_LOST');
        this.connect(
          this._host,
          this._port,
          this._username ?? '',
          this._password ?? '',
          this._serverPassword ?? '',
        );
      } else {
        this.chatService.pushSystemMessage('SERVICE.CLIENT.DISCONNECTED');
      }
    });
  }

  /**
   * Called when the player's name changes.
   *
   * @param name Player's name.
   */
  private onSetName(name: string): void {
    this.zone.run(() => {
      const effectiveName =
        this._username && this._username.length > 0 ? this._username : name;
      this.nameChange.next(effectiveName);
    });
  }

  /**
   * Called when the client's version is set.
   *
   * @param version Game's version.
   */
  private onSetVersion(version: string): void {
    version = environment.overwriteVersion || version;

    this.zone.run(() => {
      this.versionSet.next(version);
    });
  }

  getVersion(): string {
    return this.versionSet.value;
  }

  /**
   * Add listener to when the player's press the F3 key
   */
  private onDebug(isDebug: boolean): void {
    this.zone.run(() => {
      this.debugStateChange.next(isDebug);
    });
  }

  private onUpdateDebug(
    numPacketsSent: number,
    numPacketsReceived: number,
    RTT: number,
    packetLoss: number,
    sentBandwidth: number,
    receivedBandwidth: number,
  ): void {
    this.zone.run(() => {
      this.debugDataChange.next(
        new Debug(
          numPacketsSent,
          numPacketsReceived,
          RTT,
          packetLoss,
          sentBandwidth,
          receivedBandwidth,
        ),
      );
    });
  }

  private onPlayerConnected(
    playerId: number,
    username: string,
    level: number,
    cellName: string,
    avatar: string,
  ) {
    if (environment.game) {
      console.log(
        `%conPlayerConnected`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.playerConnectedChange.next(
        new Player({
          name: username,
          id: playerId,
          connected: true,
          level: level,
          cellName: cellName,
          avatar: avatar,
        }),
      );
    });
  }

  private onPlayerDisconnected(playerId: number, username: string) {
    if (environment.game) {
      console.log(
        `%conPlayerDisconnected`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.playerDisconnectedChange.next(
        new Player({
          name: username,
          id: playerId,
          connected: false,
        }),
      );
    });
  }

  private onPlayerAvatar(playerId: number, avatar: string) {
    if (environment.game) {
      console.log(
        `%conPlayerAvatar`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.avatarChange.next(
        new Player({
          id: playerId,
          avatar: avatar,
        }),
      );
    });
  }

  private onSetHealth(playerId: number, health: number) {
    this.zone.run(() => {
      this.healthChange.next(new Player({ id: playerId, health: health }));
    });
  }

  private onSetLevel(playerId: number, level: number) {
    if (environment.game) {
      console.log(
        `%conSetLevel`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.levelChange.next(new Player({ id: playerId, level: level }));
    });
  }

  private onSetCell(playerId: number, cellName: string) {
    if (environment.game) {
      console.log(
        `%conSetCell`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.cellChange.next(new Player({ id: playerId, cellName: cellName }));
    });
  }

  private onSetPlayer3dLoaded(playerId: number, health: number) {
    if (environment.game) {
      console.log(
        `%conSetPlayer3dLoaded`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.isLoadedChange.next(
        new Player({ id: playerId, isLoaded: true, health: health }),
      );
    });
  }

  private onSetPlayer3dUnloaded(playerId: number) {
    if (environment.game) {
      console.log(
        `%conSetPlayer3dUnloaded`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.isLoadedChange.next(new Player({ id: playerId, isLoaded: false }));
    });
  }

  private onSetLocalPlayerId(playerId: number) {
    if (environment.game) {
      console.log(
        `%conSetLocalPlayerId`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.localPlayerId = playerId;
      this.localPlayerIdChange.next(playerId);
    });
  }

  private onSetSyncStatus(
    isolated: boolean,
    title: string,
    detail: string,
    moreInfo?: string,
  ) {
    this.zone.run(() => {
      this.syncStatusChange.next({
        isolated,
        title,
        detail,
        moreInfo: moreInfo || '',
      });
    });
  }

  private onProtocolMismatch() {
    this.zone.run(() => {
      this.protocolMismatchChange.next(true);
    });
  }

  private onTriggerError(rawError: string) {
    this.zone.run(() => {
      const error = JSON.parse(rawError) as ErrorEvents;
      this.triggerError.next(error);
      if (error.error === 'wrong_server_password' && this._host) {
        this._serverPassword = '';
        const currentView = this.uiRepository.getView();
        const defaultReturnView =
          currentView === View.SERVER_LIST ? View.SERVER_LIST : View.CONNECT;
        const storedReturnView = this.uiRepository.getConnectReturnView();
        const returnView =
          currentView === View.CONNECT
            ? defaultReturnView
            : storedReturnView ?? defaultReturnView;
        const storedName = this.uiRepository.getConnectName();
        const connectName =
          storedName && storedName.length > 0 ? storedName : this._host;
        this.uiRepository.openConnectWithPasswordView(
          this._host,
          this._port,
          connectName,
          returnView,
          this._username,
          this._password,
        );
      } else if (error.error === 'wrong_account_password') {
        this.uiRepository.openView(View.CONNECT);
      } else if (error.error === 'duplicate_user') {
        this.uiRepository.openView(View.CONNECT);
      }
      void this.errorService.setError(error);
    });
  }

  private emoteOpenedFromInactive = false;

  private onOpenEmoteMenu(openedFromInactive?: boolean) {
    this.emoteOpenedFromInactive = !!openedFromInactive;
    this.zone.run(() => {
      this.activationStateChange.next(true);
      this.uiRepository.openView(View.EMOTES);
    });
  }

  private onToggleEmoteMenu() {
    this.zone.run(() => {
      const currentView = this.uiRepository.getView();
      if (currentView === View.EMOTES) {
        this.uiRepository.closeView();
        if (this.emoteOpenedFromInactive) {
          this.emoteOpenedFromInactive = false;
          this.deactivate();
        }
      }
    });
  }

  private onShowBanner(message: string, durationMs?: number) {
    this.zone.run(() => {
      this.overlayBannerService.show(
        {
          primary: message,
          tone: 'info',
        },
        durationMs && durationMs > 0 ? durationMs : undefined,
      );
    });
  }

  private onDummyData(data: Array<number>) {
    this.zone.run(() => {
      this.debugChange.next();
      /*
      for (const numb of data) {
        console.log(numb);
      }
      console.log(data);
      */
    });
  }

  public onPartyInfo(playerIds: Array<number>, leaderId: number) {
    if (environment.game) {
      console.log(
        `%conPartyInfo`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.partyInfoChange.next(
        new PartyInfo({
          playerIds: playerIds,
          leaderId: leaderId,
        }),
      );
    });
  }

  private onPartyCreated() {
    if (environment.game) {
      console.log(
        `%conPartyCreated`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.loadingService.setLoading(false);
      this.partyInfoChange.next(
        new PartyInfo({
          playerIds: [this.localPlayerId],
          leaderId: this.localPlayerId,
        }),
      );
    });
  }

  private onPartyLeft() {
    if (environment.game) {
      console.log(
        `%conPartyLeft`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.partyLeftChange.next();
    });
  }

  private onTeleportRequest(requesterId: number, requesterName: string): void {
    this.zone.run(() => {
      this.teleportRequestChange.next({ requesterId, requesterName });
    });
  }

  private onTeleportCountdown(
    targetPlayerId: number,
    targetName: string,
    secondsRemaining: number,
    cancelled: boolean,
    reason: string,
  ): void {
    if (environment.game) {
      console.log(
        `%conTeleportCountdown`,
        'background: #3f51b5; color: #fff; padding: 3px; font-size: 9px;',
        targetPlayerId,
        targetName,
        secondsRemaining,
        cancelled,
        reason,
      );
    }

    this.zone.run(() => {
      if (cancelled) {
        if (reason && reason.length > 0) {
          this.overlayBannerService.show(
            {
              primary: reason,
              tone: 'error',
            },
            4000,
          );
        } else {
          this.overlayBannerService.hide();
        }
        return;
      }

      const safeSeconds = Math.max(0, secondsRemaining);
      const primary = this.translocoService.translate(
        'SERVICE.OVERLAY_BANNER.TELEPORT_COUNTDOWN_TITLE',
        { name: targetName },
      );
      const secondary = this.translocoService.translate(
        'SERVICE.OVERLAY_BANNER.TELEPORT_COUNTDOWN_SUBTITLE',
        { seconds: safeSeconds },
      );
      this.overlayBannerService.show({
        primary,
        secondary,
        tone: 'info',
      });
    });
  }

  private onSetPartyPins(json: string) {
    try {
      const pins = JSON.parse(json) as Array<{
        x: unknown;
        y: unknown;
        id: unknown;
        oob?: unknown;
        name?: unknown;
        avatar?: unknown;
      }>;
      const normalized = pins.map(pin => {
        const asNumber = (value: unknown) => {
          if (typeof value === 'number') {
            return value;
          }
          const parsed = Number(value);
          return Number.isFinite(parsed) ? parsed : 0;
        };

        return {
          x: asNumber(pin.x),
          y: asNumber(pin.y),
          id: Math.trunc(asNumber(pin.id)),
          oob: Boolean(pin.oob),
          name: typeof pin.name === 'string' ? pin.name : undefined,
          avatar: typeof pin.avatar === 'string' ? pin.avatar : undefined,
        };
      });
      this.zone.run(() => this.partyPinsChange.next(normalized));
    } catch (e) {
      // ignore invalid payloads
    }
  }

  private onPartyInviteReceived(inviterId: number) {
    if (environment.game) {
      console.log(
        `%conPartyInviteReceived`,
        'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
        ...Array.from(arguments).map(v => JSON.stringify(v)),
      );
    }
    this.zone.run(() => {
      this.partyInviteReceivedChange.next(inviterId);
    });
  }

  private onTradeInviteReceived(inviterId: number, expiryTick: number) {
    if (environment.game) {
      console.log(
        `%conTradeInviteReceived`,
        'background: #FFC107; color: #000; padding: 3px; font-size: 9px;',
        inviterId,
        expiryTick,
      );
    }
    this.zone.run(() => {
      this.tradeInviteChange.next({ inviterId, expiryTick });
    });
  }

  private onTradeInviteExpired(inviterId: number) {
    if (environment.game) {
      console.log(
        `%conTradeInviteExpired`,
        'background: #FFC107; color: #000; padding: 3px; font-size: 9px;',
        inviterId,
      );
    }
    this.zone.run(() => {
      this.tradeInviteExpiredChange.next(inviterId);
    });
  }

  private onTradeStateUpdated(
    active: boolean,
    partnerId: number,
    initiatedBySelf: boolean,
    selfReady: boolean,
    partnerReady: boolean,
    selfItems: unknown[],
    partnerItems: unknown[],
    inventory: unknown[],
    countdownMs: number,
    countdownTotalMs: number,
  ) {
    if (environment.game) {
      console.log(
        `%conTradeStateUpdated`,
        'background: #FFC107; color: #000; padding: 3px; font-size: 9px;',
        active,
        partnerId,
        initiatedBySelf,
        selfReady,
        partnerReady,
        selfItems,
        partnerItems,
        inventory,
        countdownMs,
        countdownTotalMs,
      );
    }

    const normalized: TradeStatePayload = {
      active: Boolean(active),
      partnerId: Number.isFinite(partnerId) ? partnerId : 0,
      initiatedBySelf: Boolean(initiatedBySelf),
      selfReady: Boolean(selfReady),
      partnerReady: Boolean(partnerReady),
      selfItems: Array.isArray(selfItems)
        ? selfItems.map(item => this.normalizeTradeItem(item))
        : [],
      partnerItems: Array.isArray(partnerItems)
        ? partnerItems.map(item => this.normalizeTradeItem(item))
        : [],
      inventory: Array.isArray(inventory)
        ? inventory.map(item => this.normalizeTradeItem(item))
        : [],
      countdownMs: Number.isFinite(countdownMs) ? Number(countdownMs) : 0,
      countdownTotalMs: Number.isFinite(countdownTotalMs)
        ? Number(countdownTotalMs)
        : 0,
    };

    this.zone.run(() => {
      this.tradeStateChange.next(normalized);
    });
  }

  private onTradeCancelled(
    partnerId: number,
    reason: number,
    wasInitiator: boolean,
  ) {
    if (environment.game) {
      console.log(
        `%conTradeCancelled`,
        'background: #FFC107; color: #000; padding: 3px; font-size: 9px;',
        partnerId,
        reason,
        wasInitiator,
      );
    }
    this.zone.run(() => {
      this.tradeCancelledChange.next({ partnerId, reason, wasInitiator });
    });
  }

  private onTradeCompleted(partnerId: number) {
    if (environment.game) {
      console.log(
        `%conTradeCompleted`,
        'background: #FFC107; color: #000; padding: 3px; font-size: 9px;',
        partnerId,
      );
    }
    this.zone.run(() => {
      this.tradeCompletedChange.next(partnerId);
    });
  }

  private normalizeTradeItem(payload: any): TradeItemPayload {
    const modId = Number.isFinite(payload?.modId) ? Number(payload.modId) : 0;
    const baseId = Number.isFinite(payload?.baseId)
      ? Number(payload.baseId)
      : 0;
    const count = Math.max(
      0,
      Number.isFinite(payload?.count) ? Number(payload.count) : 0,
    );
    const isQuestItem = Boolean(payload?.isQuestItem);
    const fallbackName = `0x${modId.toString(16).padStart(8, '0')}:0x${baseId
      .toString(16)
      .padStart(8, '0')}`;
    const name =
      typeof payload?.name === 'string' && payload.name.length > 0
        ? payload.name
        : fallbackName;

    const item: TradeItemPayload = {
      modId,
      baseId,
      count,
      isQuestItem,
      name,
    };

    if (payload?.inventoryIndex !== undefined) {
      item.inventoryIndex = Number(payload.inventoryIndex);
    }
    if (payload?.offeredCount !== undefined) {
      item.offeredCount = Math.max(
        0,
        Number.isFinite(payload.offeredCount)
          ? Number(payload.offeredCount)
          : 0,
      );
    }
    if (payload?.isGold !== undefined) {
      item.isGold = Boolean(payload.isGold);
    }
    if (Array.isArray(payload?.details)) {
      item.details = payload.details.map((entry: any) => String(entry));
    }
    if (item.isGold === undefined) {
      item.isGold = modId === 0 && baseId === 0x0000000f;
    }
    item.raw = payload;

    return item;
  }

  /**
   * Called when the death screen is shown.
   */
  private onShowDeathScreen(secondsRemaining: number): void {
    if (environment.game) {
      console.log(
        `%conShowDeathScreen`,
        'background: #f44336; color: #fff; padding: 3px; font-size: 9px;',
        secondsRemaining,
      );
    }
    this.zone.run(() => {
      this.deathScreenChange.next(secondsRemaining);
    });
  }

  /**
   * Called when the death screen timer updates.
   */
  private onUpdateDeathTimer(secondsRemaining: number): void {
    if (environment.game) {
      console.log(
        `%conUpdateDeathTimer`,
        'background: #f44336; color: #fff; padding: 3px; font-size: 9px;',
        secondsRemaining,
      );
    }
    this.zone.run(() => {
      this.deathTimerChange.next(secondsRemaining);
    });
  }

  /**
   * Called when the respawn button is enabled.
   */
  private onEnableRespawnButton(): void {
    if (environment.game) {
      console.log(
        `%conEnableRespawnButton`,
        'background: #4caf50; color: #fff; padding: 3px; font-size: 9px;',
      );
    }
    this.zone.run(() => {
      this.respawnButtonEnabledChange.next();
    });
  }

  /**
   * Called when the death screen is hidden.
   */
  private onHideDeathScreen(): void {
    if (environment.game) {
      console.log(
        `%conHideDeathScreen`,
        'background: #2196f3; color: #fff; padding: 3px; font-size: 9px;',
      );
    }
    this.zone.run(() => {
      this.deathScreenHiddenChange.next();
    });
  }

  private onUpdateReviveVictimProgress(
    elapsedSeconds: number,
    totalSeconds: number,
    healerName: string,
  ): void {
    if (environment.game) {
      console.log(
        `%conUpdateReviveVictimProgress`,
        'background: #9c27b0; color: #fff; padding: 3px; font-size: 9px;',
        elapsedSeconds,
        totalSeconds,
      );
    }
    this.zone.run(() => {
      this.reviveVictimProgressChange.next({
        elapsedSeconds,
        totalSeconds,
        label: healerName,
      });
    });
  }

  private onStopReviveVictimProgress(): void {
    if (environment.game) {
      console.log(
        `%conStopReviveVictimProgress`,
        'background: #9c27b0; color: #fff; padding: 3px; font-size: 9px;',
      );
    }
    this.zone.run(() => {
      this.reviveVictimProgressChange.next(undefined);
    });
  }

  private onUpdateReviveHealerProgress(
    elapsedSeconds: number,
    totalSeconds: number,
  ): void {
    if (environment.game) {
      console.log(
        `%conUpdateReviveHealerProgress`,
        'background: #00acc1; color: #fff; padding: 3px; font-size: 9px;',
        elapsedSeconds,
        totalSeconds,
      );
    }
    this.zone.run(() => {
      this.reviveHealerProgressChange.next({
        elapsedSeconds,
        totalSeconds,
      });
    });
  }

  private onStopReviveHealerProgress(): void {
    if (environment.game) {
      console.log(
        `%conStopReviveHealerProgress`,
        'background: #00acc1; color: #fff; padding: 3px; font-size: 9px;',
      );
    }
    this.zone.run(() => {
      this.reviveHealerProgressChange.next(undefined);
    });
  }

  /**
   * Called when the player clicks the respawn button.
   */
  public respawnButtonClicked(): void {
    skyrimtogether.respawnButtonClicked();
  }
}
