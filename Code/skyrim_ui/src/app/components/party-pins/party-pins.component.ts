import { Component, OnDestroy } from '@angular/core';
import { Subscription, combineLatest } from 'rxjs';
import { PartyPin } from '../../models/party-pin';
import { ClientService } from '../../services/client.service';
import { PlayerListService } from '../../services/player-list.service';

@Component({
  selector: 'app-party-pins',
  templateUrl: './party-pins.component.html',
  styleUrls: ['./party-pins.component.scss'],
})
export class PartyPinsComponent implements OnDestroy {
  pins: PartyPin[] = [];
  readonly defaultAvatar = 'assets/images/group/avatar-placeholder.png';
  private sub: Subscription;

  constructor(
    private readonly client: ClientService,
    private readonly playerList: PlayerListService,
  ) {
    this.sub = combineLatest([
      this.client.partyPinsChange,
      this.playerList.playerList.asObservable(),
    ]).subscribe(([pins, list]) => {
      const players = list?.players ?? [];
      const playerMap = new Map(players.map(player => [player.id, player]));

      this.pins = pins.map(pin => {
        const player = playerMap.get(pin.id);
        const resolvedName = pin.name ?? player?.name;
        const resolvedAvatar =
          pin.avatar !== undefined ? pin.avatar : player?.avatar;

        return {
          ...pin,
          name: resolvedName,
          avatar:
            resolvedAvatar && resolvedAvatar.length > 0
              ? resolvedAvatar
              : this.defaultAvatar,
        };
      });
    });
  }

  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}
