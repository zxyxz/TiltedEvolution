import { ChatService } from '../chat.service';

export interface Command {
  readonly name: string;
  readonly executor: (args: string[]) => Promise<void>;
}

export class CommandHandler {
  private readonly commands = new Map<string, Command>();

  public constructor(private readonly chatService: ChatService) {}

  public readonly COMMAND_PREFIX = '/';

  public register(cmd: Command) {
    if (!this.commands.has(cmd.name)) {
      this.commands.set(cmd.name, cmd);
    }
  }

  public tryExecute(input: string): boolean {
    const inputWithoutPrefix = input.slice(this.COMMAND_PREFIX.length);
    const [commandName, ...args] = inputWithoutPrefix.split(' ');
    const command = this.commands.get(commandName);
    if (command) {
      command.executor(args);
      return true;
    }
    return false;
  }
}
