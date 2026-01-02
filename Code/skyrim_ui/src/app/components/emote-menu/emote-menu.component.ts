import { Component, EventEmitter, Output } from '@angular/core';
import { ClientService } from '../../services/client.service';
import { Sound, SoundService } from '../../services/sound.service';
import { UiRepository } from '../../store/ui.repository';

interface EmoteDefinition {
  key: string;
  event: string;
  tone: 'sun' | 'ember' | 'mystic' | 'bard' | 'calm' | 'ale';
  category: 'greeting' | 'music' | 'social' | 'relax' | 'misc';
}

@Component({
  selector: 'app-emote-menu',
  templateUrl: './emote-menu.component.html',
  styleUrls: ['./emote-menu.component.scss'],
})
export class EmoteMenuComponent {
  @Output() public done = new EventEmitter<void>();

  public emotes: EmoteDefinition[] = [
    {
      key: 'WAVE',
      event: 'IdleGetAttention',
      tone: 'sun',
      category: 'greeting',
    },
    {
      key: 'PRAY',
      event: 'IdlePrayCrouchedEnter',
      tone: 'mystic',
      category: 'relax',
    },
    {
      key: 'WARM_HANDS',
      event: 'IdleWarmHands',
      tone: 'ember',
      category: 'relax',
    },
    {
      key: 'SIT',
      event: 'IdleSitCrossLeggedEnter',
      tone: 'calm',
      category: 'relax',
    },
    {
      key: 'LEAN',
      event: 'IdleLeanTableEnter',
      tone: 'calm',
      category: 'relax',
    },
    {
      key: 'HANDS_BEHIND_BACK',
      event: 'IdleHandsBehindBack',
      tone: 'calm',
      category: 'social',
    },
    {
      key: 'DRINK',
      event: 'IdleTableDrinkEnter',
      tone: 'ale',
      category: 'social',
    },
    {
      key: 'EAT',
      event: 'idleEatingStandingStart',
      tone: 'sun',
      category: 'social',
    },
    {
      key: 'CLAP',
      event: 'IdleApplaud2',
      tone: 'sun',
      category: 'social',
    },
    {
      key: 'CHEER',
      event: 'IdleCivilWarCheer',
      tone: 'sun',
      category: 'greeting',
    },
    {
      key: 'LAUGH',
      event: 'IdleLaugh',
      tone: 'sun',
      category: 'social',
    },
    {
      key: 'DANCE_CICERO_1',
      event: 'IdleCiceroDance1',
      tone: 'bard',
      category: 'music',
    },
    {
      key: 'DANCE_CICERO_2',
      event: 'IdleCiceroDance2',
      tone: 'bard',
      category: 'music',
    },
    {
      key: 'DANCE_CICERO_3',
      event: 'IdleCiceroDance3',
      tone: 'bard',
      category: 'music',
    },
    { key: 'DRUM', event: 'IdleDrumStart', tone: 'bard', category: 'music' },
    { key: 'FLUTE', event: 'IdleFluteStart', tone: 'bard', category: 'music' },
    { key: 'LUTE', event: 'IdleLuteStart', tone: 'bard', category: 'music' },
    {
      key: 'WALL_LEAN',
      event: 'IdleWallLeanStart',
      tone: 'calm',
      category: 'relax',
    },
    {
      key: 'COWER',
      event: 'IdleCowerEnter',
      tone: 'mystic',
      category: 'misc',
    },
    {
      key: 'BEGGAR',
      event: 'IdleBeggar',
      tone: 'calm',
      category: 'misc',
    },
    {
      key: 'SWEEP',
      event: 'idleLooseSweepingStart',
      tone: 'ember',
      category: 'misc',
    },
    {
      key: 'STOP',
      event: 'IdleForceDefaultState',
      tone: 'calm',
      category: 'misc',
    },
  ];

  public activeCategory: EmoteDefinition['category'] = 'greeting';
  public hoveredEmote?: EmoteDefinition;
  public filteredList: EmoteDefinition[] = [];

  public constructor(
    private readonly client: ClientService,
    private readonly sound: SoundService,
    private readonly uiRepository: UiRepository,
  ) {
    const saved = this.uiRepository.getEmoteCategory();
    if (
      saved === 'greeting' ||
      saved === 'music' ||
      saved === 'social' ||
      saved === 'relax' ||
      saved === 'misc'
    ) {
      this.activeCategory = saved as EmoteDefinition['category'];
    } else {
      this.uiRepository.setEmoteCategory(this.activeCategory);
    }
    this.updateFiltered();
  }

  public categories(): Array<EmoteDefinition['category']> {
    return ['greeting', 'music', 'social', 'relax', 'misc'];
  }

  private updateFiltered(): void {
    this.filteredList = this.emotes.filter(
      e => e.category === this.activeCategory,
    );
  }

  public setCategory(category: EmoteDefinition['category']): void {
    this.activeCategory = category;
    this.uiRepository.setEmoteCategory(category);
    this.sound.play(Sound.Focus);
    this.updateFiltered();
  }

  public hover(emote?: EmoteDefinition): void {
    this.hoveredEmote = emote;
  }

  public play(emote: EmoteDefinition): void {
    this.client.playEmote(emote.event);
    this.sound.play(Sound.Focus);
    this.hoveredEmote = emote;
  }

  public emoteTrackBy(_index: number, emote: EmoteDefinition): string {
    return emote.key;
  }
}
