import {
  AfterViewInit,
  Component,
  ElementRef,
  EventEmitter,
  HostListener,
  OnDestroy,
  Output,
  ViewChild,
} from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { firstValueFrom, Subscription } from 'rxjs';
import { View } from '../../models/view.enum';
import { ClientService } from '../../services/client.service';
import { ErrorService } from '../../services/error.service';
import { Sound, SoundService } from '../../services/sound.service';
import { StoreService } from '../../services/store.service';
import { UiRepository } from '../../store/ui.repository';

@Component({
  selector: 'app-connect',
  templateUrl: './connect.component.html',
  styleUrls: ['./connect.component.scss'],
})
export class ConnectComponent implements OnDestroy, AfterViewInit {
  public address = '';
  public username = '';
  public password = '';
  public savePassword = false;

  public connecting = false;

  @Output() public done = new EventEmitter<void>();

  public constructor(
    private readonly client: ClientService,
    private readonly sound: SoundService,
    private readonly errorService: ErrorService,
    private readonly storeService: StoreService,
    private readonly translocoService: TranslocoService,
    private readonly uiRepository: UiRepository,
  ) {
    this.connectionSubscription = this.client.connectionStateChange.subscribe(
      async state => {
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
      },
    );

    this.protocolMismatchSubscription =
      this.client.protocolMismatchChange.subscribe(async state => {
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

    this.address = this.storeService.get('last_connected_address', '');
    const savedUsername = this.storeService.get('last_connected_username', '');
    const savedPassword = this.storeService.get('last_connected_password', '');
    if (savedUsername) {
      this.username = savedUsername;
    }
    if (savedPassword) {
      this.password = savedPassword;
    }
    this.savePassword =
      (!!savedUsername && savedUsername.length > 0) ||
      (!!savedPassword && savedPassword.length > 0);
  }

  public ngAfterViewInit(): void {
    setTimeout(() => {
      this.inputRef.nativeElement.focus();
    }, 100);
  }

  public ngOnDestroy(): void {
    this.connectionSubscription.unsubscribe();
    this.protocolMismatchSubscription.unsubscribe();
  }

  async connect(): Promise<void> {
    const address = this.address.trim().match(/^(.+?)(?::([0-9]+))?$/);

    if (!address) {
      this.sound.play(Sound.Fail);
      const message = await firstValueFrom(
        this.translocoService.selectTranslate(
          'COMPONENT.CONNECT.ERROR.INVALID_ADDRESS',
        ),
      );
      await this.errorService.setError(message);
      return;
    }

    this.connecting = true;

    const username = this.username.trim();

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

    this.username = username;

    this.storeService.set('last_connected_address', this.address);
    if (this.savePassword) {
      this.storeService.set('last_connected_username', username);
      this.storeService.set('last_connected_password', this.password);
    } else {
      this.storeService.remove('last_connected_username');
      this.storeService.remove('last_connected_password');
    }

    const port = address[2] ? Number.parseInt(address[2], 10) : 10578;
    const savedEntry = this.getSavedServerEntry(address[1], port);
    const serverPassword =
      savedEntry?.serverPassword ?? savedEntry?.password ?? '';

    this.sound.play(Sound.Ok);
    this.client.connect(
      address[1],
      port,
      username,
      this.password,
      serverPassword,
    );
  }

  public cancel(): void {
    this.sound.play(Sound.Cancel);
    this.done.next();
  }

  public openServerList(): void {
    this.uiRepository.openView(View.SERVER_LIST);
  }

  @ViewChild('input')
  private inputRef!: ElementRef;

  private connectionSubscription: Subscription;

  private protocolMismatchSubscription: Subscription;

  @HostListener('window:keydown.escape', ['$event'])
  // @ts-ignore
  private activate(event: KeyboardEvent): void {
    if (this.errorService.getError()) {
      this.errorService.removeError();
    } else {
      this.done.next();
    }

    event.stopPropagation();
    event.preventDefault();
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
        password?: string;
      }
    | undefined {
    const savedServerList = JSON.parse(
      this.storeService.get('savedServerList', '[]'),
    );
    const savedServer = savedServerList.find(
      (saved: any) => saved.ip === ip && saved.port === port,
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
}
