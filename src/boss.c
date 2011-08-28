/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information. 
 * ---
 * Copyright (C) 2011, Lukas Weber <laochailan@web.de>
 */

#include "boss.h"
#include "global.h"
#include <string.h>
#include <stdio.h>

Boss *create_boss(char *name, char *ani, complex pos) {
	Boss *buf = malloc(sizeof(Boss));
	memset(buf, 0, sizeof(Boss));	
	
	buf->name = malloc(strlen(name) + 1);
	strcpy(buf->name, name);
	buf->pos = pos;
		
	buf->ani = get_ani(ani);
	
	return buf;
}

void spell_opening(Boss *b, int time) {
	float y = VIEWPORT_H - 15;
	if(time > 40 && time <= 100)
		y -= (VIEWPORT_H-50)/60.0*(time-40);
	if(time > 100) {
		y = 35;
	}
	
	draw_text(AL_Right, VIEWPORT_W, y, b->current->name, _fonts.standard);
}

void draw_boss(Boss *boss) {
	draw_animation_p(creal(boss->pos), cimag(boss->pos) + 6*sin(global.frames/25.0), boss->anirow, boss->ani);
	draw_text(AL_Left, 10, 20, boss->name, _fonts.standard);
	
	if(!boss->current)
		return;
	
	if(boss->current->type == AT_Spellcard || boss->current->type == AT_SurvivalSpell)
		spell_opening(boss, global.frames - boss->current->starttime);
	
	if(boss->current->type != AT_Move) {
		char buf[16];
		snprintf(buf, sizeof(buf),  "%.2f", (boss->current->timeout - global.frames + boss->current->starttime)/(float)FPS);
		draw_text(AL_Center, VIEWPORT_W - 20, 10, buf, _fonts.standard);
		
		int nextspell, lastspell;
		for(nextspell = 0; nextspell < boss->acount; nextspell++) {
			if(boss->dmg < boss->attacks[nextspell].dmglimit && boss->attacks[nextspell].type == AT_Spellcard)
				break;
		}
		
		for(lastspell = nextspell; lastspell > 0; lastspell--) {
			if(boss->dmg > boss->attacks[lastspell].dmglimit && boss->attacks[lastspell].type == AT_Spellcard)
				break;
		}
		
		int dmgoffset = boss->attacks[lastspell].dmglimit;
		int dmgspan = boss->attacks[nextspell].dmglimit - boss->attacks[lastspell].dmglimit;
				
		glPushMatrix();
		glTranslatef(VIEWPORT_W-50, 4, 0);
		glScalef((VIEWPORT_W-60)/(float)dmgspan,1,1);
		glTranslatef(dmgoffset,0,0);
		int i;
		
		glColor4f(0,0,0,0.65);
		
		glBegin(GL_QUADS);
			glVertex3f(-boss->attacks[nextspell].dmglimit, 0, 0);
			glVertex3f(-boss->attacks[nextspell].dmglimit, 4, 0);
			glVertex3f(-boss->dmg+2, 4, 0);
			glVertex3f(-boss->dmg+2, 0, 0);
		glEnd();
		
		for(i = nextspell; i >= 0; i--) {
			if(boss->dmg > boss->attacks[i].dmglimit)
				continue;
			
			switch(boss->attacks[i].type) {
			case AT_Normal:
				glColor3f(1,1,1);
				break;
			case AT_Spellcard:
				glColor3f(1,0.65,0.65);
				break;
			case AT_SurvivalSpell:
				glColor3f(1,0.5,0.5);
			default:
				break; // never happens
			}		
			
			glBegin(GL_QUADS);
			glVertex3f(-boss->attacks[i].dmglimit, 0, 0);
			glVertex3f(-boss->attacks[i].dmglimit, 2, 0);
			glVertex3f(-boss->dmg, 2, 0);
			glVertex3f(-boss->dmg, 0, 0);
			glEnd();
		}
				
		glPopMatrix();
		
		glColor4f(1,1,1,0.7);
		
		int x = 0;
		for(i = boss->acount-1; i > nextspell; i--)
			if(boss->attacks[i].type == AT_Spellcard)
				draw_texture(x += 22, 40, "star");
	}
		
	glColor3f(1,1,1);	
}

void process_boss(Boss *boss) {
	if(boss->current) {		
		int time = global.frames - boss->current->starttime;
		
		boss->current->rule(boss, time);
				
		if(boss->current->type != AT_Move && boss->dmg >= boss->current->dmglimit)
			time = boss->current->timeout + 1;
		if(time > boss->current->timeout) {
			boss->dmg = boss->current->dmglimit + 1;
			boss->current++;
			if(boss->current - boss->attacks < boss->acount)
				start_attack(boss, boss->current);
			else
				boss->current = NULL;
		}
	}
}

void boss_death(Boss **boss) {
	petal_explosion(35, (*boss)->pos);
	free_boss(*boss);
	*boss = NULL;
	
	Projectile *p;
	for(p = global.projs; p; p = p->next)
		if(p->type == FairyProj)
			p->type = DeadProj;
	
	delete_lasers(&global.lasers);
}

void free_boss(Boss *boss) {
	del_ref(boss);
	
	free(boss->name);
	int i;
	for(i = 0; i < boss->acount; i++)
		free_attack(&boss->attacks[i]);
	if(boss->attacks)
		free(boss->attacks);
}

void free_attack(Attack *a) {
	free(a->name);
}

void start_attack(Boss *b, Attack *a) {	
	a->starttime = global.frames + ATTACK_START_DELAY;
	a->rule(b, EVENT_BIRTH);
	if(a->type == AT_Spellcard || a->type == AT_SurvivalSpell)
		play_sound("charge_generic");
	
	Projectile *p;
	for(p = global.projs; p; p = p->next)
		if(p->type == FairyProj)
			p->type = DeadProj;
}

Attack *boss_add_attack(Boss *boss, AttackType type, char *name, float timeout, int hp, BossRule rule, BossRule draw_rule) {
	boss->attacks = realloc(boss->attacks, sizeof(Attack)*(++boss->acount));
	Attack *a = &boss->attacks[boss->acount-1];
	
// 	if(!boss->current)
		boss->current = &boss->attacks[0];
	
	a->type = type;
	a->name = malloc(strlen(name)+1);
	strcpy(a->name, name);
	a->timeout = timeout * FPS;
	
	int dmg = 0;
	if(boss->acount > 1)
		dmg = boss->attacks[boss->acount - 2].dmglimit;
	
	a->dmglimit = dmg + hp;
	a->rule = rule;
	a->draw_rule = draw_rule;
	
	a->starttime = global.frames;
	
	return a;
}