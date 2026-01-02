import {
  AfterViewInit,
  Component,
  ElementRef,
  EventEmitter,
  HostListener,
  Output,
  ViewChild,
} from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { firstValueFrom, takeUntil } from 'rxjs';
import { View } from '../../models/view.enum';
import { ClientService } from '../../services/client.service';
import { ErrorService } from '../../services/error.service';
import { Sound, SoundService } from '../../services/sound.service';
import { StoreService } from '../../services/store.service';
import { UiRepository } from '../../store/ui.repository';
import { DestroyService } from '../../services/destroy.service';

@Component({
  selector: 'app-connect-password',
  templateUrl: './connect-password.component.html',
  styleUrls: ['./connect-password.component.scss'],
  providers: [DestroyService],
})
export class ConnectPasswordComponent implements AfterViewInit {
  public address = '';
  public username = '';
  public port = 10578;
  public accountPassword = '';
  public serverPassword = '';
  public savePassword = false;
  public hidePassword = true;

  public connecting = false;

  @ViewChild('input') private inputRef!: ElementRef;
  @Output() public done = new EventEmitter<void>();

  public constructor(
    private readonly destroy$: DestroyService,
    private readonly client: ClientService,
    private readonly sound: SoundService,
    private readonly errorService: ErrorService,
    private readonly storeService: StoreService,
    private readonly translocoService: TranslocoService,
    private readonly uiRepository: UiRepository,
  ) {
    this.client.connectionStateChange
      .pipe(takeUntil(this.destroy$))
      .subscribe(async state => {
        if (this.connecting) {
          this.connecting = false;

          if (state) {
            this.sound.play(Sound.Success);
            this.done.next();
          } else if (this.errorService.getError() === '') {
            // show connection error when there is no more specific error
            const message = await firstValueFrom(
              this.translocoService.selectTranslate<string>(
                'COMPONENT.CONNECT.ERROR.CONNECTION',
              ),
            );
            await this.errorService.setError(message);
          }
        }
      });

    this.client.protocolMismatchChange
      .pipe(takeUntil(this.destroy$))
      .subscribe(async state => {
        if (state) {
          this.connecting = false;
          const message = await firstValueFrom(
            this.translocoService.selectTranslate<string>(
              'COMPONENT.CONNECT.ERROR.VERSION_MISMATCH',
            ),
          );
          await this.errorService.setError(message);
        }
      });

    const connectIp = this.uiRepository.getConnectIp();
    if (connectIp) {
      this.address = connectIp;
    }

    const connectPort = this.uiRepository.getConnectPort();
    if (Number.isFinite(connectPort)) {
      this.port = connectPort;
    }

    const savedEntry = this.getSavedServerEntry(this.address, this.port);
    const storedUsername = this.storeService.get('last_connected_username', '');
    const storedAccountPassword = this.storeService.get(
      'last_connected_password',
      '',
    );
    const repoUsername = this.uiRepository.getConnectUsername();
    const repoAccountPassword = this.uiRepository.getConnectAccountPassword();

    this.username =
      (repoUsername && repoUsername.length > 0
        ? repoUsername
        : savedEntry?.username) ||
      storedUsername ||
      this.username;

    this.accountPassword =
      (repoAccountPassword && repoAccountPassword.length > 0
        ? repoAccountPassword
        : savedEntry?.accountPassword) ||
      storedAccountPassword ||
      this.accountPassword;

    this.serverPassword = savedEntry?.serverPassword ?? this.serverPassword;

    this.savePassword =
      (!!this.username && this.username.length > 0) ||
      (!!this.accountPassword && this.accountPassword.length > 0) ||
      (!!this.serverPassword && this.serverPassword.length > 0);
  }

  public ngAfterViewInit(): void {
    setTimeout(() => {
      this.inputRef.nativeElement.focus();
    }, 100);
  }

  async connect(): Promise<void> {
    if (!this.address) {
      this.sound.play(Sound.Fail);
      const message = await firstValueFrom(
        this.translocoService.selectTranslate(
          'COMPONENT.CONNECT.ERROR.INVALID_ADDRESS',
        ),
      );
      await this.errorService.setError(message);
      return;
    }

    const username = (this.username ?? '').trim();

    if (username.length === 0) {
      this.sound.play(Sound.Fail);
      const message = await firstValueFrom(
        this.translocoService.selectTranslate(
          'COMPONENT.CONNECT.ERROR.INVALID_USERNAME',
        ),
      );
      await this.errorService.setError(message);
      return;
    }

    this.connecting = true;

    this.sound.play(Sound.Ok);
    if (this.savePassword) {
      this.saveServerEntry(this.address, this.port, {
        username,
        accountPassword: this.accountPassword,
        serverPassword: this.serverPassword,
      });
      this.storeService.set('last_connected_username', username);
      this.storeService.set('last_connected_password', this.accountPassword);
    } else {
      this.removeSavedServerEntry(this.address, this.port);
      this.storeService.remove('last_connected_username');
      this.storeService.remove('last_connected_password');
    }
    this.username = username;

    this.client.connect(
      this.address,
      this.port,
      username,
      this.accountPassword,
      this.serverPassword,
    );
  }

  public cancel(): void {
    const returnView = this.uiRepository.getConnectReturnView() ?? View.CONNECT;
    this.uiRepository.openView(returnView);
  }

  private getSavedServerEntry(
    ip: string,
    port: number,
  ):
    | {
        ip: string;
        port: number;
        username?: string;
        accountPassword?: string;
        serverPassword?: string;
      }
    | undefined {
    let savedServerList = JSON.parse(
      this.storeService.get('savedServerList', '[]'),
    );
    let savedServer = savedServerList.find(
      saved => saved.ip === ip && saved.port === port,
    );

    if (savedServer) {
      if (
        savedServer.password &&
        (!savedServer.serverPassword || savedServer.serverPassword.length === 0)
      ) {
        savedServer.serverPassword = savedServer.password;
      }
      return savedServer;
    }

    return undefined;
  }

  private saveServerEntry(
    ip: string,
    port: number,
    entry: {
      username: string;
      accountPassword: string;
      serverPassword: string;
    },
  ): void {
    let savedServerList = JSON.parse(
      this.storeService.get('savedServerList', '[]'),
    );
    let savedServer = savedServerList.find(
      saved => saved.ip === ip && saved.port === port,
    );

    if (!savedServer) {
      savedServer = { ip, port };
      savedServerList.push(savedServer);
    }

    savedServer.username = entry.username;
    savedServer.accountPassword = entry.accountPassword;
    savedServer.serverPassword = entry.serverPassword;
    if (savedServer.password) {
      delete savedServer.password;
    }

    this.storeService.set('savedServerList', JSON.stringify(savedServerList));
  }

  private removeSavedServerEntry(ip: string, port: number): void {
    let savedServerList = JSON.parse(
      this.storeService.get('savedServerList', '[]'),
    );
    const filtered = savedServerList.filter(
      saved => !(saved.ip === ip && saved.port === port),
    );

    this.storeService.set('savedServerList', JSON.stringify(filtered));
  }

  @HostListener('window:keydown.escape', ['$event'])
  // @ts-ignore
  private activate(event: KeyboardEvent): void {
    if (this.errorService.getError()) {
      this.errorService.removeError();
    } else {
      this.cancel();
    }

    event.stopPropagation();
    event.preventDefault();
  }
}
