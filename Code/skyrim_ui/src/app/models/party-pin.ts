export interface PartyPin {
  x: number; // screen-space X in pixels
  y: number; // screen-space Y in pixels
  id: number; // player id
  oob?: boolean; // true if placed as an edge indicator (different worldspace/interior)
  name?: string;
  avatar?: string;
}
