import { ChangeDetectionStrategy, Component } from '@angular/core';
import { OverlayBannerService } from '../../services/overlay-banner.service';

@Component({
  selector: 'app-overlay-banner',
  templateUrl: './overlay-banner.component.html',
  styleUrls: ['./overlay-banner.component.scss'],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class OverlayBannerComponent {
  banner$ = this.overlayBannerService.banner$;

  constructor(
    private readonly overlayBannerService: OverlayBannerService,
  ) {}
}
