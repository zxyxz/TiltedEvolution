import { Component, OnInit, NgZone } from '@angular/core';
import { BehaviorSubject, Observable } from 'rxjs';
import { ClientService } from '../../services/client.service';

interface HealerReviveState {
  progress: number;
  remainingSeconds: number;
  totalSeconds: number;
}

@Component({
  selector: 'app-revive-progress',
  templateUrl: './revive-progress.component.html',
  styleUrls: ['./revive-progress.component.scss'],
})
export class ReviveProgressComponent implements OnInit {
  state$: Observable<HealerReviveState | undefined>;
  private stateSubject = new BehaviorSubject<HealerReviveState | undefined>(
    undefined,
  );

  constructor(
    private readonly clientService: ClientService,
    private readonly ngZone: NgZone,
  ) {
    this.state$ = this.stateSubject.asObservable();
  }

  ngOnInit(): void {
    this.clientService.reviveHealerProgressChange.subscribe((payload) => {
      this.ngZone.run(() => {
        if (!payload) {
          this.stateSubject.next(undefined);
          return;
        }

        const progress =
          payload.totalSeconds > 0
            ? Math.min(payload.elapsedSeconds / payload.totalSeconds, 1)
            : 0;

        this.stateSubject.next({
          progress,
          remainingSeconds: Math.max(
            payload.totalSeconds - payload.elapsedSeconds,
            0,
          ),
          totalSeconds: payload.totalSeconds,
        });
      });
    });
  }
}
