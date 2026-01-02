import {
  ChangeDetectionStrategy,
  Component,
  NgZone,
  OnDestroy,
} from '@angular/core';
import { DomSanitizer, SafeUrl } from '@angular/platform-browser';
import { BehaviorSubject, Observable, Subscription, combineLatest } from 'rxjs';
import { filter, map, pluck, startWith } from 'rxjs/operators';
import { Player } from 'src/app/models/player';
import { ClientService } from 'src/app/services/client.service';
import { GroupService } from 'src/app/services/group.service';
import { LoadingService } from 'src/app/services/loading.service';
import { PlayerListService } from 'src/app/services/player-list.service';

@Component({
  selector: 'app-party-menu',
  templateUrl: './party-menu.component.html',
  styleUrls: ['./party-menu.component.scss'],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class PartyMenuComponent implements OnDestroy {
  isLoading$ = this.loadingService.getLoading();
  members$ = combineLatest([
    this.groupService.selectMembers(),
    this.clientService.localPlayerIdChange,
  ]).pipe(
    map(([members, localId]) =>
      members.filter(member => member.id !== localId),
    ),
  );
  memberCount$ = this.groupService.selectMembersLength(true);
  group$ = this.groupService.group.asObservable();
  isLaunchPartyDisabled$: Observable<boolean>;
  invitations$: Observable<Player[]>;
  isPartyLeader$: Observable<boolean>;
  readonly defaultAvatarPath = 'assets/images/group/avatar-placeholder.png';
  readonly defaultAvatar: SafeUrl;
  private readonly profilePreviewSubject = new BehaviorSubject<string | null>(
    null,
  );
  profilePreview$ = this.profilePreviewSubject.asObservable();
  profilePreviewSafeUrl$: Observable<SafeUrl>;
  uploadError$ = new BehaviorSubject<string | null>(null);
  private readonly subscriptions = new Subscription();
  private readonly targetImageSize = 128;
  private readonly maxEncodedBytes = 256 * 1024;
  private readonly maxUploadBytes = 2 * 1024 * 1024;

  constructor(
    private readonly groupService: GroupService,
    private readonly loadingService: LoadingService,
    private readonly playerListService: PlayerListService,
    private readonly clientService: ClientService,
    private readonly zone: NgZone,
    private readonly sanitizer: DomSanitizer,
  ) {
    this.defaultAvatar = this.sanitizer.bypassSecurityTrustUrl(
      this.defaultAvatarPath,
    );
    this.profilePreviewSafeUrl$ = this.profilePreviewSubject.pipe(
      map(preview =>
        preview
          ? this.sanitizer.bypassSecurityTrustUrl(preview)
          : this.defaultAvatar,
      ),
    );

    this.invitations$ = this.playerListService.playerList.asObservable().pipe(
      filter(playerlist => !!playerlist),
      pluck('players'),
      map(players => players.filter(player => player.hasInvitedLocalPlayer)),
      startWith([]),
    );
    this.isPartyLeader$ = this.groupService.group
      .asObservable()
      .pipe(
        map(
          group =>
            group.isEnabled && group.owner == this.clientService.localPlayerId,
        ),
      );
    this.isLaunchPartyDisabled$ = this.groupService
      .selectMembersLength(false)
      .pipe(map(count => count > 1));

    this.subscriptions.add(
      this.playerListService.playerList.asObservable().subscribe(list => {
        const localId = this.clientService.localPlayerId;
        if (!list || localId === undefined) {
          return;
        }

        const localPlayer = list.players.find(player => player.id === localId);
        if (localPlayer) {
          this.profilePreviewSubject.next(localPlayer.avatar || null);
        }
      }),
    );

    this.subscriptions.add(
      this.clientService.avatarChange.subscribe(player => {
        if (player.id === this.clientService.localPlayerId) {
          this.profilePreviewSubject.next(player.avatar || null);
        }
      }),
    );

    this.subscriptions.add(
      this.clientService.connectionStateChange.subscribe(isConnected => {
        if (!isConnected) {
          this.profilePreviewSubject.next(null);
        }
      }),
    );
  }

  public launchParty() {
    this.groupService.launch();
  }

  public leave() {
    this.groupService.leave();
  }

  public requestTeleport(playerId: number) {
    this.clientService.requestTeleport(playerId);
  }

  public respondTeleport(playerId: number, accepted: boolean) {
    this.clientService.respondTeleportRequest(playerId, accepted);
  }

  public acceptPartyInvite(inviterId: number) {
    this.playerListService.acceptPartyInvite(inviterId);
  }

  public kickMember(playerId: number) {
    this.clientService.kickPartyMember(playerId);
  }

  public changeLeader(playerId: number) {
    this.clientService.changePartyLeader(playerId);
  }

  public onProfilePictureSelected(event: Event) {
    const input = event.target as HTMLInputElement;
    if (!input?.files || input.files.length === 0) {
      return;
    }

    const file = input.files[0];
    this.uploadError$.next(null);
    const allowedMimeTypes = ['image/png', 'image/jpeg'];
    const allowedExtensions = ['png', 'jpg', 'jpeg'];

    if (file.size > this.maxUploadBytes) {
      this.uploadError$.next('COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_SIZE');
      input.value = '';
      return;
    }

    if (file.type && !allowedMimeTypes.includes(file.type.toLowerCase())) {
      this.uploadError$.next('COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_TYPE');
      input.value = '';
      return;
    }

    const extension = file.name.split('.').pop()?.toLowerCase();
    if (!file.type && extension && !allowedExtensions.includes(extension)) {
      this.uploadError$.next('COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_TYPE');
      input.value = '';
      return;
    }

    const reader = new FileReader();
    reader.onload = () => {
      const result = typeof reader.result === 'string' ? reader.result : null;
      this.zone.run(() => {
        if (!result) {
          this.uploadError$.next(
            'COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_PROCESS',
          );
          return;
        }

        this.normalizeImage(result)
          .then(normalized => {
            const size = this.estimateDataUrlSize(normalized);
            if (size > this.maxEncodedBytes) {
              this.uploadError$.next(
                'COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_SIZE',
              );
              return;
            }

            this.uploadError$.next(null);
            this.profilePreviewSubject.next(normalized);
            this.clientService.setProfilePicture(normalized);
          })
          .catch(() => {
            this.uploadError$.next(
              'COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_PROCESS',
            );
          });
      });
    };
    reader.onerror = () => {
      this.zone.run(() => {
        this.uploadError$.next(
          'COMPONENT.PARTY_MENU.PROFILE_PICTURE.ERROR_TYPE',
        );
      });
    };
    reader.readAsDataURL(file);
    input.value = '';
  }

  public clearProfilePicture() {
    this.uploadError$.next(null);
    this.profilePreviewSubject.next(null);
    this.clientService.setProfilePicture('');
  }

  public resetUploadError() {
    if (this.uploadError$.getValue()) {
      this.uploadError$.next(null);
    }
  }

  private normalizeImage(dataUrl: string): Promise<string> {
    return new Promise((resolve, reject) => {
      const image = new Image();
      image.onload = () => {
        try {
          const canvas = document.createElement('canvas');
          const ctx = canvas.getContext('2d');
          if (!ctx) {
            reject(new Error('Canvas context unavailable'));
            return;
          }

          const size = this.targetImageSize;
          canvas.width = size;
          canvas.height = size;

          ctx.clearRect(0, 0, size, size);

          const scale = Math.min(size / image.width, size / image.height);
          const drawWidth = image.width * scale;
          const drawHeight = image.height * scale;
          const dx = (size - drawWidth) / 2;
          const dy = (size - drawHeight) / 2;

          ctx.drawImage(image, dx, dy, drawWidth, drawHeight);

          resolve(canvas.toDataURL('image/png'));
        } catch (error) {
          reject(error);
        }
      };
      image.onerror = () => reject(new Error('Image decode failed'));
      image.src = dataUrl;
    });
  }

  private estimateDataUrlSize(dataUrl: string): number {
    const commaIndex = dataUrl.indexOf(',');
    if (commaIndex === -1) {
      return dataUrl.length;
    }
    const base64 = dataUrl.slice(commaIndex + 1);
    return Math.floor((base64.length * 3) / 4);
  }

  ngOnDestroy() {
    this.subscriptions.unsubscribe();
  }
}
