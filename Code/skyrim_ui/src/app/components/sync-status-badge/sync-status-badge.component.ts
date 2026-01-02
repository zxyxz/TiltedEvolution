import { Component, OnDestroy } from '@angular/core';
import { Subject, takeUntil } from 'rxjs';
import { ClientService } from '../../services/client.service';

@Component({
  selector: 'app-sync-status-badge',
  templateUrl: './sync-status-badge.component.html',
  styleUrls: ['./sync-status-badge.component.scss'],
})
export class SyncStatusBadgeComponent implements OnDestroy {
  public readonly status$ = this.client.syncStatusChange.asObservable();
  public showMoreInfo = false;
  private readonly destroy$ = new Subject<void>();
  private lastInfo = '';

  public constructor(private readonly client: ClientService) {
    this.status$.pipe(takeUntil(this.destroy$)).subscribe(status => {
      if (!status.isolated || status.moreInfo !== this.lastInfo) {
        this.showMoreInfo = false;
      }
      this.lastInfo = status.moreInfo;
    });
  }

  public toggleMoreInfo() {
    this.showMoreInfo = !this.showMoreInfo;
  }

  public ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }
}
