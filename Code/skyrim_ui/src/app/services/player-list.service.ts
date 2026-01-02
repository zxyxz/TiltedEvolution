import { Injectable, OnDestroy } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject, Subscription } from 'rxjs';
import { Player } from '../models/player';
import { PlayerList } from '../models/player-list';
import { PlayerManagerTab } from '../models/player-manager-tab.enum';
import { View } from '../models/view.enum';
import { UiRepository } from '../store/ui.repository';
import { ClientService } from './client.service';
import { PopupNotificationService } from './popup-notification.service';

@Injectable({
  providedIn: 'root',
})
export class PlayerListService implements OnDestroy {
  public playerList = new BehaviorSubject<PlayerList | undefined>(undefined);
  private readonly defaultAvatar = 'assets/images/group/avatar-placeholder.png';

  private debugSubscription: Subscription;
  private connectionSubscription: Subscription;
  private playerConnectedSubscription: Subscription;
  private playerDisconnectedSubscription: Subscription;
  private memberKickedSubscription: Subscription;
  private cellSubscription: Subscription;
  private partyInviteReceivedSubscription: Subscription;
  private teleportRequestSubscription: Subscription;
  private teleportRequestHandledSubscription: Subscription;
  private avatarSubscription: Subscription;
  private nameSubscription: Subscription;
  private localPlayerSubscription: Subscription;

  private isConnect = false;

  constructor(
    private readonly clientService: ClientService,
    private readonly popupNotificationService: PopupNotificationService,
    private readonly uiRepository: UiRepository,
    private readonly translocoService: TranslocoService,
  ) {
    this.onDebug();
    this.onConnectionStateChanged();
    this.onPlayerConnected();
    this.onPlayerDisconnected();
    this.onMemberKicked();
    this.onCellChange();
    this.onPartyInviteReceived();
    this.onTeleportRequest();
    this.onTeleportRequestHandled();
    this.onAvatarChange();
    this.onLocalPlayerMetadata();
  }

  ngOnDestroy() {
    this.debugSubscription.unsubscribe();
    this.connectionSubscription.unsubscribe();
    this.playerConnectedSubscription.unsubscribe();
    this.playerDisconnectedSubscription.unsubscribe();
    this.cellSubscription.unsubscribe();
    this.partyInviteReceivedSubscription.unsubscribe();
    this.teleportRequestSubscription.unsubscribe();
    this.teleportRequestHandledSubscription.unsubscribe();
    this.avatarSubscription.unsubscribe();
    this.nameSubscription.unsubscribe();
    this.localPlayerSubscription.unsubscribe();
  }

  private onDebug() {
    this.debugSubscription = this.clientService.debugChange.subscribe(() => {
      console.log(this.playerList);
    });
  }

  private onConnectionStateChanged() {
    this.connectionSubscription =
      this.clientService.connectionStateChange.subscribe((connect: boolean) => {
        if (this.isConnect == connect) {
          return;
        }
        this.isConnect = connect;
        this.playerList.next(undefined);

        this.updatePlayerList();
        if (connect) {
          this.ensureLocalPlayerEntry();
        }
      });
  }

  private onPlayerConnected() {
    this.playerConnectedSubscription =
      this.clientService.playerConnectedChange.subscribe((player: Player) => {
        const playerList = this.getPlayerList();

        if (playerList) {
          const existing = playerList.players.find(
            entry => entry.id === player.id,
          );
          if (existing) {
            existing.name = player.name;
            existing.level = player.level;
            existing.cellName = player.cellName;
            existing.connected = player.connected;
            existing.online = player.online;
            existing.avatar =
              player.avatar && player.avatar.length > 0
                ? player.avatar
                : existing.avatar || this.defaultAvatar;
          } else {
            playerList.players.push(
              new Player({
                ...player,
                avatar:
                  player.avatar && player.avatar.length > 0
                    ? player.avatar
                    : this.defaultAvatar,
              }),
            );
          }
          this.playerList.next(playerList);
        }
      });
  }

  private onPlayerDisconnected() {
    this.playerDisconnectedSubscription =
      this.clientService.playerDisconnectedChange.subscribe(
        (playerDisco: Player) => {
          const playerList = this.getPlayerList();

          if (playerList) {
            playerList.players = playerList.players.filter(
              player => player.id !== playerDisco.id,
            );

            this.playerList.next(playerList);
          }
        },
      );
  }

  private onMemberKicked() {
    this.memberKickedSubscription =
      this.clientService.memberKickedChange.subscribe((playerId: number) => {
        const playerList = this.getPlayerList();
        const players = playerList.players.find(
          player => player.id !== playerId,
        );
        players.hasBeenInvited = false;
      });
  }

  private onCellChange() {
    this.cellSubscription = this.clientService.cellChange.subscribe(
      (player: Player) => {
        const playerList = this.getPlayerList();

        if (playerList) {
          const p = this.getPlayerById(player.id);
          if (p) {
            p.cellName = player.cellName;
          }
        }
      },
    );
  }

  private onPartyInviteReceived() {
    this.partyInviteReceivedSubscription =
      this.clientService.partyInviteReceivedChange.subscribe(
        async (inviterId: number) => {
          const playerList = this.getPlayerList();

          if (playerList) {
            const invitingPlayer = this.getPlayerById(inviterId);
            invitingPlayer.hasInvitedLocalPlayer = true;
            this.playerList.next(playerList);
            this.popupNotificationService.addPartyInvite(
              invitingPlayer.name,
              () => this.acceptPartyInvite(inviterId),
            );
          }
        },
      );
  }

  private onTeleportRequest() {
    this.teleportRequestSubscription =
      this.clientService.teleportRequestChange.subscribe(
        ({ requesterId, requesterName }) => {
          const playerList = this.getPlayerList();
          const player = playerList
            ? this.getPlayerById(requesterId)
            : undefined;
          const displayName = player?.name ?? requesterName;

          if (player && playerList) {
            player.hasTeleportRequest = true;
            this.playerList.next(playerList);
          }

          this.popupNotificationService.addTeleportRequest(
            displayName,
            () => {
              if (player && playerList) {
                player.hasTeleportRequest = false;
                this.playerList.next(playerList);
              }
              this.clientService.respondTeleportRequest(requesterId, true);
            },
            () => {
              if (player && playerList) {
                player.hasTeleportRequest = false;
                this.playerList.next(playerList);
              }
              this.clientService.respondTeleportRequest(requesterId, false);
            },
          );
        },
      );
  }

  private onTeleportRequestHandled() {
    this.teleportRequestHandledSubscription =
      this.clientService.teleportRequestHandledChange.subscribe(
        ({ requesterId }) => {
          const playerList = this.playerList.getValue();
          if (!playerList) {
            return;
          }

          const player = playerList.players.find(
            existing => existing.id === requesterId,
          );
          if (player) {
            player.hasTeleportRequest = false;
            this.playerList.next(playerList);
          }
        },
      );
  }

  private onAvatarChange() {
    this.avatarSubscription = this.clientService.avatarChange.subscribe(
      (player: Player) => {
        const playerList = this.playerList.getValue();
        if (!playerList) {
          return;
        }

        const existing = playerList.players.find(
          entry => entry.id === player.id,
        );

        if (!existing) {
          return;
        }

        existing.avatar = player.avatar;
        this.playerList.next(playerList);
      },
    );
  }

  private onLocalPlayerMetadata() {
    this.nameSubscription = this.clientService.nameChange.subscribe(() => {
      this.ensureLocalPlayerEntry();
    });

    this.localPlayerSubscription =
      this.clientService.localPlayerIdChange.subscribe(() => {
        this.ensureLocalPlayerEntry();
      });
  }

  private ensureLocalPlayerEntry() {
    const localId = this.clientService.localPlayerId;
    if (localId === undefined || localId === null) {
      return;
    }

    const playerList = this.playerList.getValue() ?? this.getPlayerList();
    if (!playerList) {
      return;
    }

    const displayName = this.clientService.nameChange.getValue();
    let existing = playerList.players.find(player => player.id === localId);

    if (!existing) {
      existing = new Player({
        id: localId,
        name: displayName && displayName.length > 0 ? displayName : 'You',
        connected: true,
        online: true,
        cellName: '',
        isLoaded: true,
        avatar: this.defaultAvatar,
      });
      playerList.players.push(existing);
    } else {
      if (displayName && displayName.length > 0) {
        existing.name = displayName;
      }
      if (!existing.avatar) {
        existing.avatar = this.defaultAvatar;
      }
    }

    existing.connected = true;
    existing.online = true;
    existing.isLoaded = true;

    this.playerList.next(playerList);
  }

  public getLocalPlayer(): Player {
    let localPlayerId = this.clientService.localPlayerId;
    return this.getPlayerById(localPlayerId);
  }

  public getPlayerList() {
    return this.createPlayerList(this.playerList.getValue());
  }

  public getListLength(): number {
    return this.getPlayerList() ? this.getPlayerList().players.length : 0;
  }

  private createPlayerList(playerList: PlayerList | undefined) {
    if (!playerList) {
      playerList = new PlayerList();
      this.playerList.next(playerList);
    }
    return this.playerList.getValue();
  }

  public updatePlayerList() {
    this.playerList.next(this.playerList.getValue());
  }

  public sendPartyInvite(inviteeId: number) {
    const playerList = this.getPlayerList();

    if (playerList) {
      this.getPlayerById(inviteeId).hasBeenInvited = true;

      this.updatePlayerList();

      this.clientService.createPartyInvite(inviteeId);
    }
  }

  public acceptPartyInvite(inviterId: number) {
    const playerList = this.getPlayerList();

    if (playerList) {
      this.getPlayerById(inviterId).hasInvitedLocalPlayer = false;

      this.clientService.acceptPartyInvite(inviterId);

      this.playerList.next(playerList);
    }
  }

  public getPlayerById(playerId: number): Player {
    return this.getPlayerList().players.find(player => player.id === playerId);
  }

  public resetHasBeenInvitedFlags() {
    const playerList = this.getPlayerList();

    if (playerList) {
      for (const player of playerList.players) {
        player.hasBeenInvited = false;
      }

      this.updatePlayerList();
    }
  }
}
