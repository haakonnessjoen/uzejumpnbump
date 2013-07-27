/*
 *  Jump'n'Bump
 *  Graphics Copyright (C) 1998 Brainchild Design
 *  Jump'n'Bump Code Copyright (C) 2013 Håkon Nessjøen
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Uzebox is a reserved trade mark
*/


#include <stdbool.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <uzebox.h>


#include "data/bunnymap.inc"
#include "data/bunnytiles.inc"

#include "data/patches.inc"

#define ACTION_IDLE 0
#define ACTION_WALK 1
#define ACTION_STOPPING 2
#define ACTION_JUMP 3
#define ACTION_DYING 4

#define NUM_PLAYERS 2
#define NUM_BLOOD 6

unsigned char processControls(void);
void PerformActions();

extern const char waves[];
extern void SetColorBurstOffset(unsigned char value);

unsigned char delay=0;
unsigned int mainframe = 0;

#define FLOOR_POS (169-32+16)

struct animblood {
	unsigned char x;
	unsigned char y;
	unsigned char pos;
	unsigned char gravity;
	signed char dir;
	unsigned char active;
	unsigned char freq;
};

struct players {
	unsigned char spritex;
	unsigned char spritey;
	unsigned char frame;
	unsigned char action;
	unsigned char sprDir;
	unsigned char jmpPos;
	unsigned short score;
	char directionX;
	bool stopping;
};

struct animblood blood[NUM_BLOOD];
struct players player[NUM_PLAYERS];
enum spritename {
	SPR_IDLE,
	SPR_WALK1,
	SPR_WALK2,
	SPR_WALK3,
	SPR_JUMP,
	SPR_FLY,
	SPR_LAND,
	SPR_BLINK,
	SPR_SQUASH
};

/* TODO: Get this to work with PROGMEM, somehow I can't get progmem ** pointers to work, even with pgm_read_word(&player1sprites[sprite]) */
const char *player1sprites[] = {
	map_bunnyidle,
	map_bunnywalk1,
	map_bunnywalk2,
	map_bunnywalk3,
	map_bunnyjump,
	map_bunnyfly,
	map_bunnyland,
	map_bunnyblink,
	map_bunnysquash
};

const char *player2sprites[] = {
	map_bunnyidle2,
	map_bunnywalk21,
	map_bunnywalk22,
	map_bunnywalk23,
	map_bunnyjump2,
	map_bunnyfly2,
	map_bunnyland2,
	map_bunnyblink2,
	map_bunnysquash2
};

void setSprite(unsigned char playerIdx, enum spritename sprite, unsigned char flags) {

	switch (playerIdx) {
		case 0:
			MapSprite2(playerIdx * 4, player1sprites[sprite], flags);
			break;
		case 1:
			MapSprite2(playerIdx * 4, player2sprites[sprite], flags);
			break;
	};
}

void showScore() {
	for (unsigned char i = 0; i < 3; ++i) {
		/* KISS: Instead of pow() */
		unsigned char delim = (i == 0 ? 100 : (i == 1 ? 10 : 1));

		unsigned char score = (player[0].score / delim) % 10;
		SetTile(38 + i, VRAM_TILES_V, pgm_read_byte(&font1[2 + score]));
		SetTile(38+32 + i, VRAM_TILES_V, pgm_read_byte(&font1[2 + score + 10]));

		score = (player[1].score / delim) % 10;
		SetTile(52 + i, VRAM_TILES_V, pgm_read_byte(&font1[2 + score]));
		SetTile(52+32 + i, VRAM_TILES_V, pgm_read_byte(&font1[2 + score + 10]));
	}
}

int main(){	
	ClearVram();

	InitMusicPlayer(patches);
	SetMasterVolume(0x40);

	SetSpritesTileTable(bunnytiles);
	SetFontTilesIndex(BUNNYMAP_SIZE);

	SetTileTable(bunnymap);
	
	DrawMap2(0,VRAM_TILES_V, bunnyhud);

	memset(blood, 0, sizeof(blood));

	unsigned char c;
	for(int y=0;y<23;y++){
		for(int x=0;x<30;x++){
			int ly = y+4;
			c=pgm_read_byte(&(map_level[(ly*MAP_LEVEL_WIDTH)+x+2]));
			SetTile(x,y+1,c);
		}	
	}

	player[0].spritey = FLOOR_POS;
	player[0].action = ACTION_IDLE;
	player[0].sprDir = 1;
	player[0].spritex = 50;

	player[1].spritey = FLOOR_POS;
	player[1].action = ACTION_IDLE;
	player[1].sprDir = 1;
	player[1].spritex = 100;

	/* Idle sprites for player 1 & 2 */
	setSprite(0, SPR_IDLE, 0);
	setSprite(1, SPR_IDLE, 0);

	MapSprite2(8,map_blood1,0);
	MapSprite2(9,map_blood2,0);
	MapSprite2(10,map_blood3,0);
	MapSprite2(11,map_blood4,0);
	MapSprite2(12,map_blood1,SPRITE_FLIP_X);
	MapSprite2(13,map_blood2,SPRITE_FLIP_X);

	Scroll(0,-1);

	Screen.scrollY=0;
	Screen.overlayHeight=OVERLAY_LINES;
	
	while (1) {
		WaitVsync(1);
	
		mainframe++;
		processControls();

		PerformActions();
		MoveSprite(0,player[0].spritex, player[0].spritey, 2, 2);
		MoveSprite(4,player[1].spritex, player[1].spritey, 2, 2);	
		
		showScore();
	}		
	
}

/* Borrowed from mario demo, needs to be rewritten */
void loadNextStripe(){
	static unsigned int srcX=30;
	static unsigned char destX=30;

	for(int y=0;y<23;y++){
		SetTile(destX,y+1,pgm_read_byte(&(map_level[((y+4)*MAP_LEVEL_WIDTH)+srcX+2])));		
	}

	srcX++;
	if(srcX>=MAP_LEVEL_WIDTH)srcX=0;

	destX++;
	if(destX>=32)destX=0;
}

unsigned char collide(unsigned char left1, unsigned char top1, unsigned char left2, unsigned char top2) {
	
	unsigned char right1 = left1 + 16;
	unsigned char bottom1 = top1 + 16;
	unsigned char right2 = left2 + 16;
	unsigned char bottom2 = top2 + 16;
	
	if (bottom1 < top2) return(0);
	if (top1 > bottom2) return(0);

	if (right1 < left2) return(0);
	if (left1 > right2) return(0);

	return(1);

};

void randomBlood(unsigned char index, unsigned char playerIdx) {
	blood[index].active = 1;
	blood[index].dir = (rand() % 3) - 1;
	blood[index].gravity = (rand() % 3) + 1;
	blood[index].pos = 0;
	blood[index].x = player[playerIdx].spritex;
	blood[index].y = player[playerIdx].spritey;
	blood[index].freq = (rand() % 4) + 1;
}

void PerformActions(){
	
	/* Handle blood splatter */
	for (int i = 0; i < NUM_BLOOD; ++i) {
		if (blood[i].active) {
			MoveSprite(8+i, blood[i].x - ((blood[i].pos / blood[i].freq) * blood[i].dir), blood[i].y + 16 - (pgm_read_byte(&(waves[blood[i].pos+=3])) / blood[i].gravity), 1, 1);
			
			if (blood[i].pos == 129) {
				blood[i].active = 0;
				MoveSprite(8+i, -8, -8, 1, 1);
			}
		}
	}

	/* Handle bunnies */
	for (unsigned char playerIdx = 0; playerIdx < NUM_PLAYERS; ++playerIdx) {
		char sprFlags=(player[playerIdx].sprDir !=1 ? SPRITE_FLIP_X : 0);
		unsigned char action = player[playerIdx].action;
		unsigned char frame = player[playerIdx].frame;
		
		if (player[playerIdx].action == ACTION_WALK && player[playerIdx].stopping == true && player[playerIdx].frame >= 18) {
			setSprite(playerIdx, SPR_IDLE, sprFlags);
			player[playerIdx].action = ACTION_IDLE;
			action = ACTION_IDLE;
		}

		switch(action){
			case ACTION_IDLE:
				player[playerIdx].spritey = FLOOR_POS;

				/* Cute blinking of eyes */
				if ((mainframe % 500) > 490) {
					setSprite(playerIdx, SPR_BLINK, sprFlags);
				} else if ((mainframe % 500) == 0) {
					setSprite(playerIdx, SPR_IDLE, sprFlags);
				}
				break;
			
			case ACTION_WALK:
				player[playerIdx].spritey = FLOOR_POS;
			
				if (frame >= 0)
					setSprite(playerIdx, SPR_WALK1, sprFlags);
				
				if (frame >= 6) {
					setSprite(playerIdx, SPR_WALK2, sprFlags);
				}
				if (frame >= 12)
					setSprite(playerIdx, SPR_WALK3, sprFlags);

				if (frame >= 5 && frame <= 10) {
					player[playerIdx].spritey = FLOOR_POS - 3;
				} else {
					player[playerIdx].spritey = FLOOR_POS;
				}

				//if (frame % 2 == 0)
					player[playerIdx].spritex += player[playerIdx].directionX;

				player[playerIdx].frame++;

				if (player[playerIdx].frame > 18)
					player[playerIdx].frame = 0;


				break;
			case ACTION_DYING:
			
				if (frame >= 3) {
					player[playerIdx].spritex = 45;
					player[playerIdx].spritey = FLOOR_POS;
					setSprite(playerIdx, SPR_IDLE, 0);
					player[playerIdx].directionX = 0;
					player[playerIdx].sprDir = 1;
					player[playerIdx].action = ACTION_IDLE;
				}
			
				player[playerIdx].frame++;
				break;

			case ACTION_JUMP:

				player[playerIdx].spritey = FLOOR_POS - ( pgm_read_byte(&(waves[player[playerIdx].jmpPos])) /3  );

				if (frame < 12) {
					setSprite(playerIdx, SPR_JUMP, sprFlags);

				} else if (frame < 33) {
					setSprite(playerIdx, SPR_FLY, sprFlags);

				} else if (frame < 43) {
					setSprite(playerIdx, SPR_LAND, sprFlags);

				} else if (frame == 43) {			
					player[playerIdx].spritey = FLOOR_POS;
					setSprite(playerIdx, SPR_IDLE, sprFlags);
					player[playerIdx].action = ACTION_IDLE;
				}

				//if (frame % 2 == 0)
					player[playerIdx].spritex += player[playerIdx].directionX;

				player[playerIdx].jmpPos += 3;
				player[playerIdx].frame++;

				for (int i = 0; i < NUM_PLAYERS; ++i) {
					if (i != playerIdx && collide(player[i].spritex, player[i].spritey, player[playerIdx].spritex, player[playerIdx].spritey) && player[playerIdx].spritey < player[i].spritey && player[playerIdx].frame >= 33 && player[i].action != ACTION_DYING) {

						player[playerIdx].score ++;

						//TriggerNote(3, 1, 23, 0x7f); /* Why does not PCM work? */
						TriggerFx(2, 0xff, true);
						
						/* Spawn some blood */
						randomBlood(0, i);
						randomBlood(1, i);
						randomBlood(2, i);
						randomBlood(3, i);
						randomBlood(4, i);
						randomBlood(5, i);

						setSprite(i, SPR_SQUASH, player[i].sprDir != 1 ? SPRITE_FLIP_X : 0);
						player[i].action = ACTION_DYING;
						player[i].frame = 0;
						/* Sprite is misaligned */
						MoveSprite(i * 4,player[i].spritex, player[i].spritey - 1, 2, 2);	
					}
				}

				break;
		};
	}
}

unsigned char processControls(void){
	static unsigned char scroll=0;
	unsigned int joy;
	
	for (unsigned char playerIdx = 0; playerIdx < NUM_PLAYERS; ++playerIdx) {
		joy = ReadJoypad(playerIdx);

		unsigned char action = player[playerIdx].action;

		if (action == ACTION_DYING)
			continue;
		
		if(joy & BTN_A){
			if(action != ACTION_JUMP) {
				player[playerIdx].action = ACTION_JUMP;
				player[playerIdx].frame = 0;
				player[playerIdx].jmpPos = 0;

				/* Play jump sound, use "restart" flag "false", so you hear both bunnies */
				TriggerFx(0,0x66,false);
			}
		}
	
		if (joy & BTN_RIGHT) {
			if(action == ACTION_IDLE){
				player[playerIdx].action = ACTION_WALK;
				player[playerIdx].frame = 0;
				player[playerIdx].stopping = false;
			}
			player[playerIdx].sprDir = 1;
			player[playerIdx].directionX = 1;
		
			/* Borrowed from mario demo, needs to be rewritten */
			if (player[playerIdx].spritex >= 110){
				player[playerIdx].directionX = 0;

				/* TODO: FIX */
				if (joy & BTN_B){
					Scroll(2,0);
					scroll+=2;
				} else {
					Scroll(1,0);
					scroll++;
				}
			
				if (scroll >= 8){
					loadNextStripe();
					scroll=0;
				}
			}
		}
		if (joy & BTN_LEFT) {
			if (action == ACTION_IDLE){
				player[playerIdx].action = ACTION_WALK;
				player[playerIdx].frame = 0;
				player[playerIdx].stopping = false;
			}
			player[playerIdx].sprDir = -1;
			player[playerIdx].directionX = -1;

			/* Borrowed from mario demo, needs to be rewritten */
			if (player[playerIdx].spritex < 1){
				player[playerIdx].directionX = 0;
			}
		}
		
		if ((joy & (BTN_LEFT | BTN_RIGHT)) == 0){
			player[playerIdx].stopping = true;
			player[playerIdx].directionX = 0;
		}
	}
}