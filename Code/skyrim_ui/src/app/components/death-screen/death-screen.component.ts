import { Component, OnInit, NgZone } from '@angular/core';
import { Observable, BehaviorSubject } from 'rxjs';
import { ClientService } from '../../services/client.service';

interface ReviveUiState {
  progress: number;
  remainingSeconds: number;
  totalSeconds: number;
  healerName?: string;
}

@Component({
  selector: 'app-death-screen',
  templateUrl: './death-screen.component.html',
  styleUrls: ['./death-screen.component.scss'],
})
export class DeathScreenComponent implements OnInit {
  isVisible$: Observable<boolean>;
  secondsRemaining$: Observable<number>;
  isRespawnButtonEnabled$: Observable<boolean>;
  reviveState$: Observable<ReviveUiState | undefined>;

  private visibilitySubject = new BehaviorSubject<boolean>(false);
  private timerSubject = new BehaviorSubject<number>(0);
  private buttonEnabledSubject = new BehaviorSubject<boolean>(false);
  private reviveStateSubject = new BehaviorSubject<ReviveUiState | undefined>(
    undefined,
  );

  constructor(
    private readonly clientService: ClientService,
    private readonly ngZone: NgZone,
  ) {
    // Initialize with false/0 values
    this.isVisible$ = this.visibilitySubject.asObservable();
    this.secondsRemaining$ = this.timerSubject.asObservable();
    this.isRespawnButtonEnabled$ = this.buttonEnabledSubject.asObservable();
    this.reviveState$ = this.reviveStateSubject.asObservable();
  }

  ngOnInit(): void {
    // Track when death screen is shown - capture the initial timer value
    this.clientService.deathScreenChange.subscribe(
      (secondsRemaining: number) => {
        this.ngZone.run(() => {
          this.timerSubject.next(secondsRemaining);
          this.visibilitySubject.next(true);
          this.buttonEnabledSubject.next(false);
        });
      },
    );

    // Track timer updates
    this.clientService.deathTimerChange.subscribe(
      (secondsRemaining: number) => {
        this.ngZone.run(() => {
          this.timerSubject.next(secondsRemaining);
        });
      },
    );

    // Track respawn button enabled
    this.clientService.respawnButtonEnabledChange.subscribe(() => {
      this.ngZone.run(() => {
        this.buttonEnabledSubject.next(true);
      });
    });

    // Track death screen hidden
    this.clientService.deathScreenHiddenChange.subscribe(() => {
      this.ngZone.run(() => {
        this.visibilitySubject.next(false);
        this.timerSubject.next(0);
        this.buttonEnabledSubject.next(false);
        this.reviveStateSubject.next(undefined);
      });
    });

    this.clientService.reviveVictimProgressChange.subscribe(payload => {
      this.ngZone.run(() => {
        if (!payload) {
          this.reviveStateSubject.next(undefined);
          return;
        }

        const progress =
          payload.totalSeconds > 0
            ? Math.min(payload.elapsedSeconds / payload.totalSeconds, 1)
            : 0;
        const remaining = Math.max(
          payload.totalSeconds - payload.elapsedSeconds,
          0,
        );

        this.reviveStateSubject.next({
          progress,
          remainingSeconds: remaining,
          totalSeconds: payload.totalSeconds,
          healerName: payload.label,
        });
      });
    });
  }

  onRespawnClicked(): void {
    try {
      this.clientService.respawnButtonClicked();
    } catch (error) {
      console.error('Error calling respawn button:', error);
    }
  }
}
