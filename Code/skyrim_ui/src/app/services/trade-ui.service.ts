import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
import { TranslocoService } from '@ngneat/transloco';

import {
  ClientService,
  TradeCancellationPayload,
  TradeInvitePayload,
  TradeItemPayload,
  TradeStatePayload,
} from './client.service';
import { PlayerListService } from './player-list.service';
import { PopupNotificationService } from './popup-notification.service';
import { PlayerList } from '../models/player-list';
import { Player } from '../models/player';
import { UiRepository } from '../store/ui.repository';
import { View } from '../models/view.enum';

export interface TradeInventoryItemView {
  index: number;
  name: string;
  modId: number;
  baseId: number;
  available: number;
  offered: number;
  isQuestItem: boolean;
  isGold: boolean;
  details: string[];
  key: string;
  source: TradeItemPayload;
}

export interface TradeOfferItemView {
  index?: number;
  name: string;
  modId: number;
  baseId: number;
  count: number;
  isQuestItem: boolean;
  details: string[];
  key: string;
}

export interface TradeSessionView {
  active: boolean;
  partnerId: number;
  partnerName: string;
  initiatedBySelf: boolean;
  selfReady: boolean;
  partnerReady: boolean;
  selfOffer: TradeOfferItemView[];
  partnerOffer: TradeOfferItemView[];
  inventory: TradeInventoryItemView[];
  countdownMs: number;
  countdownTotalMs: number;
  countdownProgress: number;
}

@Injectable({
  providedIn: 'root',
})
export class TradeUiService {
  private readonly sessionSubject = new BehaviorSubject<
    TradeSessionView | undefined
  >(undefined);

  public readonly session$ = this.sessionSubject.asObservable();
  private readonly pendingOutgoing = new Set<number>();
  private readonly pendingOutgoingSubject = new BehaviorSubject<
    ReadonlySet<number>
  >(new Set());
  public readonly pendingOutgoing$ = this.pendingOutgoingSubject.asObservable();

  private latestState?: TradeStatePayload;
  private readonly playerMap = new Map<number, Player>();
  private previousView: View | null = null;
  private readonly fallbackCountdownMs = 4000;

  private readonly cancellationReasonKeys: Record<number, string> = {
    0: 'SERVICE.TRADE.CANCELLED_REASONS.DECLINED',
    1: 'SERVICE.TRADE.CANCELLED_REASONS.CANCELLED',
    2: 'SERVICE.TRADE.CANCELLED_REASONS.PARTNER_BUSY',
    3: 'SERVICE.TRADE.CANCELLED_REASONS.SELF_BUSY',
    4: 'SERVICE.TRADE.CANCELLED_REASONS.PLAYER_LEFT',
    5: 'SERVICE.TRADE.CANCELLED_REASONS.TIMEOUT',
    6: 'SERVICE.TRADE.CANCELLED_REASONS.FAILED_VALIDATION',
  };

  constructor(
    private readonly clientService: ClientService,
    private readonly playerListService: PlayerListService,
    private readonly popupNotificationService: PopupNotificationService,
    private readonly transloco: TranslocoService,
    private readonly uiRepository: UiRepository,
  ) {
    this.clientService.tradeInviteChange.subscribe(invite =>
      this.handleTradeInvite(invite),
    );
    this.clientService.tradeInviteExpiredChange.subscribe(inviterId =>
      this.handleTradeInviteExpired(inviterId),
    );
    this.clientService.tradeStateChange.subscribe(state =>
      this.handleTradeState(state),
    );
    this.clientService.tradeCancelledChange.subscribe(payload =>
      this.handleTradeCancelled(payload),
    );
    this.clientService.tradeCompletedChange.subscribe(partnerId =>
      this.handleTradeCompleted(partnerId),
    );

    this.playerListService.playerList.subscribe(list =>
      this.refreshPlayerMap(list),
    );
  }

  public sendInvite(playerId: number): void {
    this.clientService.sendTradeInvite(playerId);
    this.pendingOutgoing.add(playerId);
    this.emitPendingOutgoing();
    const partnerName = this.resolvePlayerName(playerId);
    this.popupNotificationService.addTradeInviteSent(partnerName, () =>
      this.cancelInvite(playerId),
    );
  }

  public acceptInvite(playerId: number): void {
    this.clientService.respondTradeInvite(playerId, true);
  }

  public declineInvite(playerId: number): void {
    this.clientService.respondTradeInvite(playerId, false);
  }

  public cancelInvite(playerId: number): void {
    this.removePendingInvite(playerId);
    this.clientService.cancelTrade();
  }

  public cancelTrade(): void {
    this.clientService.cancelTrade();
    this.clearPendingInvites();
    this.closePopup();
  }

  public setReady(ready: boolean): void {
    this.clientService.setTradeReady(ready);
  }

  public updateOfferFromInput(index: number, value: string | number): void {
    const parsed =
      typeof value === 'number'
        ? value
        : Number.isFinite(Number(value))
        ? Number(value)
        : NaN;
    if (!Number.isFinite(parsed)) {
      return;
    }
    this.setOfferCount(index, parsed);
  }

  public addToOffer(index: number, delta: number): void {
    const item = this.sessionSubject
      .getValue()
      ?.inventory.find(entry => entry.index === index);
    if (!item) return;
    this.setOfferCount(index, item.offered + delta);
  }

  public offerAll(index: number): void {
    const item = this.sessionSubject
      .getValue()
      ?.inventory.find(entry => entry.index === index);
    if (!item) return;
    this.setOfferCount(index, item.available);
  }

  public clearOffer(index: number): void {
    this.setOfferCount(index, 0);
  }

  public closePopup(): void {
    if (this.uiRepository.getView() === View.TRADE) {
      if (this.previousView !== null && this.previousView !== View.TRADE) {
        this.uiRepository.openView(this.previousView);
      } else {
        this.uiRepository.openView(null);
      }
    }
    this.previousView = null;
  }

  public getDisplayName(playerId: number): string {
    return this.resolvePlayerName(playerId);
  }

  private setOfferCount(index: number, value: number): void {
    if (!this.latestState || !this.latestState.active) {
      return;
    }

    const inventoryEntry = this.latestState.inventory.find(
      item => item.inventoryIndex === index,
    );
    if (!inventoryEntry) {
      return;
    }

    const bounded = Math.max(
      0,
      Math.min(Math.floor(value), inventoryEntry.count),
    );

    if (bounded === Math.max(0, inventoryEntry.offeredCount ?? 0)) {
      return;
    }

    const selections = this.latestState.inventory
      .filter(item => item.inventoryIndex !== undefined)
      .map(item => ({
        index: item.inventoryIndex as number,
        count:
          item.inventoryIndex === index
            ? bounded
            : Math.max(0, item.offeredCount ?? 0),
      }))
      .filter(entry => entry.count > 0);

    inventoryEntry.offeredCount = bounded;
    if (this.latestState) {
      this.latestState.selfReady = false;
      this.latestState.partnerReady = false;
    }
    this.refreshSessionView();

    this.clientService.updateTradeOffer(selections);
  }

  private handleTradeInvite(invite: TradeInvitePayload): void {
    const partnerName = this.resolvePlayerName(invite.inviterId);
    this.popupNotificationService.addTradeInvite(
      partnerName,
      () => this.acceptInvite(invite.inviterId),
      () => this.declineInvite(invite.inviterId),
    );
  }

  private handleTradeInviteExpired(_inviterId: number): void {
    // Nothing to do beyond removing notification hooks; invites are handled through popups.
  }

  private handleTradeState(state?: TradeStatePayload): void {
    if (state && Array.isArray(state.inventory)) {
      state.inventory.forEach((entry, idx) => {
        (entry as any).inventoryIndex = idx;
      });
    }

    this.latestState = state;

    if (!state || !state.active) {
      this.sessionSubject.next(undefined);
      this.closePopup();
      return;
    }

    this.removePendingInvite(state.partnerId);
    this.openPopupIfNeeded();
    this.sessionSubject.next(this.buildSessionView(state));
  }

  private handleTradeCancelled(payload: TradeCancellationPayload): void {
    this.removePendingInvite(payload.partnerId);
    const partnerName = this.resolvePlayerName(payload.partnerId);
    const reasonKey =
      this.cancellationReasonKeys[payload.reason] ??
      'SERVICE.TRADE.CANCELLED_REASONS.UNKNOWN';
    const reason = this.transloco.translate(reasonKey);

    this.popupNotificationService.addTradeCancelled(partnerName, reason);
    this.latestState = undefined;
    this.sessionSubject.next(undefined);
    this.closePopup();
  }

  private handleTradeCompleted(partnerId: number): void {
    this.removePendingInvite(partnerId);
    const partnerName = this.resolvePlayerName(partnerId);
    this.popupNotificationService.addTradeCompleted(partnerName);
    this.latestState = undefined;
    this.sessionSubject.next(undefined);
    this.closePopup();
  }

  private refreshPlayerMap(list?: PlayerList): void {
    this.playerMap.clear();
    if (list?.players) {
      list.players.forEach(player => this.playerMap.set(player.id, player));
    }

    if (this.latestState?.active) {
      this.sessionSubject.next(this.buildSessionView(this.latestState));
    }
  }

  private buildSessionView(state: TradeStatePayload): TradeSessionView {
    const partnerName = this.resolvePlayerName(state.partnerId);
    const countdownActive = (state.countdownMs ?? 0) > 0;
    const countdownMs = Math.max(0, state.countdownMs ?? 0);
    const totalMs = countdownActive
      ? state.countdownTotalMs && state.countdownTotalMs > 0
        ? state.countdownTotalMs
        : this.fallbackCountdownMs
      : 0;
    const countdownProgress =
      countdownActive && totalMs > 0
        ? Math.max(0, Math.min(1, 1 - countdownMs / totalMs))
        : 0;

    const rawInventory = (state.inventory ?? []).filter(
      item => item.inventoryIndex !== undefined,
    );

    const inventory: TradeInventoryItemView[] = rawInventory
      .map((item, idx) => this.createInventoryItem(item, idx))
      .sort((a, b) => a.name.localeCompare(b.name));

    const inventoryBuckets = new Map<string, TradeInventoryItemView[]>();
    for (const item of inventory) {
      const bucket = inventoryBuckets.get(item.key) ?? [];
      bucket.push(item);
      inventoryBuckets.set(item.key, bucket);
      item.offered = 0;
    }

    const selfOffer: TradeOfferItemView[] = [];
    for (const payload of state.selfItems ?? []) {
      const key = this.buildEntryKey(payload);
      const bucket = inventoryBuckets.get(key);
      let inventoryItem: TradeInventoryItemView | undefined;
      if (bucket && bucket.length > 0) {
        inventoryItem = bucket.shift();
        if (bucket.length === 0) {
          inventoryBuckets.delete(key);
        } else {
          inventoryBuckets.set(key, bucket);
        }
      }

      const offer = this.createOfferItem(payload);
      offer.count = Math.max(0, payload.count);
      if (inventoryItem) {
        offer.index = inventoryItem.index;
        inventoryItem.offered = Math.min(inventoryItem.available, offer.count);
      }
      selfOffer.push(offer);
    }

    const partnerOffer: TradeOfferItemView[] = (state.partnerItems ?? []).map(
      payload => {
        const offer = this.createOfferItem(payload);
        offer.count = Math.max(0, payload.count);
        return offer;
      },
    );

    selfOffer.sort((a, b) => a.name.localeCompare(b.name));
    partnerOffer.sort((a, b) => a.name.localeCompare(b.name));

    return {
      active: state.active,
      partnerId: state.partnerId,
      partnerName,
      initiatedBySelf: state.initiatedBySelf,
      selfReady: state.selfReady,
      partnerReady: state.partnerReady,
      selfOffer,
      partnerOffer,
      inventory,
      countdownMs,
      countdownTotalMs: totalMs,
      countdownProgress,
    };
  }

  private openPopupIfNeeded(): void {
    if (this.uiRepository.getView() !== View.TRADE) {
      this.previousView = this.uiRepository.getView();
      this.uiRepository.openView(View.TRADE);
    }
  }

  private refreshSessionView(): void {
    if (this.latestState && this.latestState.active)
      this.sessionSubject.next(this.buildSessionView(this.latestState));
  }

  private createInventoryItem(
    payload: TradeItemPayload,
    index: number,
  ): TradeInventoryItemView {
    const resolvedIndex =
      (payload as any).inventoryIndex !== undefined
        ? Number((payload as any).inventoryIndex)
        : index;
    const name = this.resolveItemName(payload);
    const isGold = this.isGold(payload);
    const details = this.buildItemDetails(payload);
    return {
      index: resolvedIndex,
      name,
      modId: payload.modId,
      baseId: payload.baseId,
      available: Math.max(0, payload.count),
      offered: Math.max(0, payload.offeredCount ?? 0),
      isQuestItem: Boolean(payload.isQuestItem),
      isGold,
      details,
      key: this.buildEntryKey(payload),
      source: payload,
    };
  }

  private emitPendingOutgoing(): void {
    this.pendingOutgoingSubject.next(new Set(this.pendingOutgoing));
  }

  private removePendingInvite(playerId: number | undefined): void {
    if (playerId === undefined) {
      return;
    }
    if (this.pendingOutgoing.delete(playerId)) {
      this.emitPendingOutgoing();
    }
  }

  private clearPendingInvites(): void {
    if (this.pendingOutgoing.size === 0) {
      return;
    }
    this.pendingOutgoing.clear();
    this.emitPendingOutgoing();
  }

  private createOfferItem(payload: TradeItemPayload): TradeOfferItemView {
    return {
      name: this.resolveItemName(payload),
      modId: payload.modId,
      baseId: payload.baseId,
      count: 0,
      isQuestItem: Boolean(payload.isQuestItem),
      details: this.buildItemDetails(payload),
      key: this.buildEntryKey(payload),
    };
  }

  private resolvePlayerName(playerId: number): string {
    if (!playerId) {
      return this.transloco.translate('SERVICE.TRADE.UNKNOWN_PLAYER', {
        id: playerId,
      });
    }

    const player = this.playerMap.get(playerId);
    if (player) {
      return player.name;
    }

    return this.transloco.translate('SERVICE.TRADE.UNKNOWN_PLAYER', {
      id: playerId,
    });
  }

  private resolveItemName(payload: TradeItemPayload): string {
    const raw: any = payload as any;
    const customName = raw?.raw?.CustomName ?? raw?.CustomName;
    if (typeof customName === 'string' && customName.length > 0) {
      return customName;
    }

    if (typeof payload.name === 'string' && payload.name.length > 0) {
      return payload.name;
    }

    return this.getFallbackItemName(payload);
  }

  private buildItemDetails(payload: TradeItemPayload): string[] {
    const details: string[] = [];
    const raw: any = (payload as any).raw ?? payload;

    if (Array.isArray(payload.details)) {
      for (const entry of payload.details) {
        details.push(String(entry));
      }
    }

    if (payload.isQuestItem) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.QUEST_ITEM'),
      );
    }

    const customName = raw?.CustomName;
    if (typeof customName === 'string' && customName.length > 0) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.CUSTOM_NAME'),
      );
    }

    const hasEnchant = this.hasEnchantData(raw);
    if (hasEnchant) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.ENCHANTED'),
      );
    }

    if (raw?.ExtraEnchantRemoveUnequip) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.ENCHANT_REMOVE'),
      );
    }

    if (typeof raw?.ExtraCharge === 'number' && raw.ExtraCharge > 0) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.CHARGE', {
          value: Math.round(raw.ExtraCharge),
        }),
      );
    }

    if (typeof raw?.ExtraPoisonCount === 'number' && raw.ExtraPoisonCount > 0) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.POISON', {
          count: raw.ExtraPoisonCount,
        }),
      );
    }

    if (typeof raw?.ExtraSoulLevel === 'number' && raw.ExtraSoulLevel > 0) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.SOUL_LEVEL', {
          level: raw.ExtraSoulLevel,
        }),
      );
    }

    if (
      typeof raw?.ExtraHealth === 'number' &&
      Math.abs(raw.ExtraHealth) > 0.01
    ) {
      details.push(
        this.transloco.translate('COMPONENT.TRADE_MENU.DETAILS.EXTRA_HEALTH', {
          value: Math.round(raw.ExtraHealth),
        }),
      );
    }

    return Array.from(new Set(details));
  }

  private buildEntryKey(payload: TradeItemPayload): string {
    const raw: any = (payload as any).raw ?? payload;
    const enchantId = raw?.ExtraEnchantId ?? {};
    const poisonId = raw?.ExtraPoisonId ?? {};
    const toFinite = (value: unknown): number => {
      const parsed = Number(value);
      return Number.isFinite(parsed) ? parsed : 0;
    };
    const enchantSignature =
      this.hasEnchantData(raw) && raw?.EnchantData
        ? JSON.stringify(raw.EnchantData)
        : '';
    const parts = [
      payload.modId,
      payload.baseId,
      toFinite(raw?.ExtraCharge ?? 0).toFixed(3),
      toFinite(raw?.ExtraHealth ?? 0).toFixed(3),
      toFinite(raw?.ExtraSoulLevel ?? 0),
      toFinite(raw?.ExtraPoisonCount ?? 0),
      toFinite(enchantId.ModId ?? enchantId.ModID ?? 0),
      toFinite(enchantId.BaseId ?? enchantId.BaseID ?? 0),
      toFinite(poisonId.ModId ?? poisonId.ModID ?? 0),
      toFinite(poisonId.BaseId ?? poisonId.BaseID ?? 0),
      enchantSignature,
      raw?.CustomName ?? '',
    ];
    return parts.join('|');
  }

  private isGold(payload: TradeItemPayload): boolean {
    return payload.modId === 0 && payload.baseId === 0x0000000f;
  }

  private getFallbackItemName(payload: TradeItemPayload): string {
    return `0x${payload.modId.toString(16).padStart(8, '0')}:0x${payload.baseId
      .toString(16)
      .padStart(8, '0')}`;
  }

  private hasEnchantData(raw: any): boolean {
    if (!raw) {
      return false;
    }

    const toFiniteNumber = (value: unknown): number => {
      const parsed = Number(value);
      return Number.isFinite(parsed) ? parsed : 0;
    };

    const enchantId = raw.ExtraEnchantId ?? raw.extraEnchantId ?? {};
    const hasId =
      toFiniteNumber(enchantId.ModId ?? enchantId.ModID ?? 0) !== 0 ||
      toFiniteNumber(enchantId.BaseId ?? enchantId.BaseID ?? 0) !== 0;

    const effects = raw.EnchantData?.Effects ?? raw.enchantData?.effects;
    const hasEffects =
      Array.isArray(effects) &&
      effects.some(effect => {
        if (!effect) {
          return false;
        }
        const magnitude = toFiniteNumber(
          effect.Magnitude ?? effect.magnitude ?? 0,
        );
        const duration = toFiniteNumber(
          effect.Duration ?? effect.duration ?? 0,
        );
        const area = toFiniteNumber(effect.Area ?? effect.area ?? 0);
        const effectId = effect.EffectId ?? effect.effectId ?? {};
        const effectIdPresent =
          toFiniteNumber(effectId.ModId ?? effectId.ModID ?? 0) !== 0 ||
          toFiniteNumber(effectId.BaseId ?? effectId.BaseID ?? 0) !== 0;

        return (
          effectIdPresent || magnitude !== 0 || duration !== 0 || area !== 0
        );
      });

    return hasId || hasEffects;
  }
}
