import { Injectable, OnDestroy } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

export type OverlayBannerTone = 'info' | 'error';

export interface OverlayBanner {
  primary: string;
  secondary?: string;
  tone?: OverlayBannerTone;
}

@Injectable({
  providedIn: 'root',
})
export class OverlayBannerService implements OnDestroy {
  private readonly bannerSubject = new BehaviorSubject<OverlayBanner | null>(
    null,
  );
  public readonly banner$ = this.bannerSubject.asObservable();

  private hideTimeout: number | undefined;

  ngOnDestroy(): void {
    this.clearTimeout();
  }

  show(banner: OverlayBanner, durationMs?: number): void {
    this.clearTimeout();
    this.bannerSubject.next({ tone: 'info', ...banner });
    if (durationMs && durationMs > 0) {
      this.hideTimeout = window.setTimeout(() => {
        this.hide();
      }, durationMs);
    }
  }

  hide(): void {
    this.clearTimeout();
    this.bannerSubject.next(null);
  }

  private clearTimeout(): void {
    if (this.hideTimeout !== undefined) {
      window.clearTimeout(this.hideTimeout);
      this.hideTimeout = undefined;
    }
  }
}
