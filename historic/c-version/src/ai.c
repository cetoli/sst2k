#include "sst.h"

static bool tryexit(coord look, feature ienm, int loccom, bool irun) 
/* a bad guy attempts to bug out */
{
    int n;
    coord iq;

    iq.x = game.quadrant.x+(look.x+(QUADSIZE-1))/QUADSIZE - 1;
    iq.y = game.quadrant.y+(look.y+(QUADSIZE-1))/QUADSIZE - 1;
    if (!VALID_QUADRANT(iq.x,iq.y) ||
	game.state.galaxy[iq.x][iq.y].supernova ||
	game.state.galaxy[iq.x][iq.y].klingons > MAXKLQUAD-1)
	return false; /* no can do -- neg energy, supernovae, or >MAXKLQUAD-1 Klingons */
    if (ienm == IHR)
	return false; /* Romulans cannot escape! */
    if (!irun) {
	/* avoid intruding on another commander's territory */
	if (ienm == IHC) {
	    for (n = 1; n <= game.state.remcom; n++)
		if (same(game.state.kcmdr[n],iq))
		    return false;
	    /* refuse to leave if currently attacking starbase */
	    if (same(game.battle, game.quadrant))
		return false;
	}
	/* don't leave if over 1000 units of energy */
	if (game.kpower[loccom] > 1000.0)
	    return false;
    }
    // print escape message and move out of quadrant.
    // We know this if either short or long range sensors are working
    if (!damaged(DSRSENS) || !damaged(DLRSENS) ||
	game.condition == docked) {
	crmena(true, ienm, sector, game.ks[loccom]);
	prout(_(" escapes to %s (and regains strength)."),
	      cramlc(quadrant, iq));
    }
    /* handle local matters related to escape */
    game.quad[game.ks[loccom].x][game.ks[loccom].y] = IHDOT;
    game.ks[loccom] = game.ks[game.nenhere];
    game.kavgd[loccom] = game.kavgd[game.nenhere];
    game.kpower[loccom] = game.kpower[game.nenhere];
    game.kdist[loccom] = game.kdist[game.nenhere];
    game.klhere--;
    game.nenhere--;
    if (game.condition != docked)
	newcnd();
    /* Handle global matters related to escape */
    game.state.galaxy[game.quadrant.x][game.quadrant.y].klingons--;
    game.state.galaxy[iq.x][iq.y].klingons++;
    if (ienm==IHS) {
	game.ishere = false;
	game.iscate = false;
	game.ientesc = false;
	game.isatb = 0;
	schedule(FSCMOVE, 0.2777);
	unschedule(FSCDBAS);
	game.state.kscmdr=iq;
    }
    else {
	for (n = 1; n <= game.state.remcom; n++) {
	    if (same(game.state.kcmdr[n], game.quadrant)) {
		game.state.kcmdr[n]=iq;
		break;
	    }
	}
	game.comhere = false;
    }
    return true; /* success */
}

/*************************************************************************
The bad-guy movement algorithm:

1. Enterprise has "force" based on condition of phaser and photon torpedoes.
If both are operating full strength, force is 1000. If both are damaged,
force is -1000. Having shields down subtracts an additional 1000.

2. Enemy has forces equal to the energy of the attacker plus
100*(K+R) + 500*(C+S) - 400 for novice through good levels OR
346*K + 400*R + 500*(C+S) - 400 for expert and emeritus.

Attacker Initial energy levels (nominal):
        Klingon    Romulan    Commander   Super-Commander
Novice    400        700        1200        
Fair      425        750        1250
Good      450        800        1300        1750
Expert    475        850        1350        1875
Emeritus  500        900        1400        2000
VARIANCE   75        200         200         200

Enemy vessels only move prior to their attack. In Novice - Good games
only commanders move. In Expert games, all enemy vessels move if there
is a commander present. In Emeritus games all enemy vessels move.

3. If Enterprise is not docked, an agressive action is taken if enemy
forces are 1000 greater than Enterprise.

Agressive action on average cuts the distance between the ship and
the enemy to 1/4 the original.

4.  At lower energy advantage, movement units are proportional to the
advantage with a 650 advantage being to hold ground, 800 to move forward
1, 950 for two, 150 for back 4, etc. Variance of 100.

If docked, is reduced by roughly 1.75*game.skill, generally forcing a
retreat, especially at high skill levels.

5.  Motion is limited to skill level, except for SC hi-tailing it out.
**************************************************************************/

static void movebaddy(coord com, int loccom, feature ienm)
/* tactical movement for the bad guys */
{
    int motion, mdist, nsteps, mx, my, ll;
    coord next, look;
    int krawlx, krawly;
    bool success, irun = false;
    int attempts;
    /* This should probably be just game.comhere + game.ishere */
    int nbaddys = game.skill >= SKILL_EXPERT ?
	(int)((game.comhere*2 + game.ishere*2+game.klhere*1.23+game.irhere*1.5)/2.0):
	(game.comhere + game.ishere);
    double dist1, forces;

    dist1 = game.kdist[loccom];
    mdist = dist1 + 0.5; /* Nearest integer distance */

    /* If SC, check with spy to see if should hi-tail it */
    if (ienm==IHS &&
	(game.kpower[loccom] <= 500.0 || (game.condition==docked && !damaged(DPHOTON)))) {
	irun = true;
	motion = -QUADSIZE;
    }
    else {
	/* decide whether to advance, retreat, or hold position */
	forces = game.kpower[loccom]+100.0*game.nenhere+400*(nbaddys-1);
	if (!game.shldup)
	    forces += 1000; /* Good for enemy if shield is down! */
	if (!damaged(DPHASER) || !damaged(DPHOTON)) {
	    if (damaged(DPHASER)) /* phasers damaged */
		forces += 300.0;
	    else
		forces -= 0.2*(game.energy - 2500.0);
	    if (damaged(DPHOTON)) /* photon torpedoes damaged */
		forces += 300.0;
	    else
		forces -= 50.0*game.torps;
	}
	else {
	    /* phasers and photon tubes both out! */
	    forces += 1000.0;
	}
	motion = 0;
	if (forces <= 1000.0 && game.condition != docked) /* Typical situation */
	    motion = ((forces+200.0*Rand())/150.0) - 5.0;
	else {
	    if (forces > 1000.0) /* Very strong -- move in for kill */
		motion = (1.0-square(Rand()))*dist1 + 1.0;
	    if (game.condition==docked && (game.options & OPTION_BASE)) /* protected by base -- back off ! */
		motion -= game.skill*(2.0-square(Rand()));
	}
	if (idebug)
	    proutn("=== MOTION = %d, FORCES = %1.2f, ", motion, forces);
	/* don't move if no motion */
	if (motion==0)
	    return;
	/* Limit motion according to skill */
	if (abs(motion) > game.skill)
	    motion = (motion < 0) ? -game.skill : game.skill;
    }
    /* calculate preferred number of steps */
    nsteps = motion < 0 ? -motion : motion;
    if (motion > 0 && nsteps > mdist)
	nsteps = mdist; /* don't overshoot */
    if (nsteps > QUADSIZE)
	nsteps = QUADSIZE; /* This shouldn't be necessary */
    if (nsteps < 1)
	nsteps = 1; /* This shouldn't be necessary */
    if (idebug) {
	proutn("NSTEPS = %d:", nsteps);
    }
    /* Compute preferred values of delta X and Y */
    mx = game.sector.x - com.x;
    my = game.sector.y - com.y;
    if (2.0 * abs(mx) < abs(my))
	mx = 0;
    if (2.0 * abs(my) < abs(game.sector.x-com.x))
	my = 0;
    if (mx != 0)
	mx = mx*motion < 0 ? -1 : 1;
    if (my != 0)
	my = my*motion < 0 ? -1 : 1;
    next = com;
    /* main move loop */
    for (ll = 0; ll < nsteps; ll++) {
	if (idebug)
	    proutn(" %d", ll+1);
	/* Check if preferred position available */
	look.x = next.x + mx;
	look.y = next.y + my;
	krawlx = mx < 0 ? 1 : -1;
	krawly = my < 0 ? 1 : -1;
	success = false;
	attempts = 0; /* Settle mysterious hang problem */
	while (attempts++ < 20 && !success) {
	    if (look.x < 1 || look.x > QUADSIZE) {
		if (motion < 0 && tryexit(look, ienm, loccom, irun))
		    return;
		if (krawlx == mx || my == 0)
		    break;
		look.x = next.x + krawlx;
		krawlx = -krawlx;
	    }
	    else if (look.y < 1 || look.y > QUADSIZE) {
		if (motion < 0 && tryexit(look, ienm, loccom, irun))
		    return;
		if (krawly == my || mx == 0)
		    break;
		look.y = next.y + krawly;
		krawly = -krawly;
	    }
	    else if ((game.options & OPTION_RAMMING) && game.quad[look.x][look.y] != IHDOT) {
		/* See if we should ram ship */
		if (game.quad[look.x][look.y] == game.ship &&
		    (ienm == IHC || ienm == IHS)) {
		    ram(true, ienm, com);
		    return;
		}
		if (krawlx != mx && my != 0) {
		    look.x = next.x + krawlx;
		    krawlx = -krawlx;
		}
		else if (krawly != my && mx != 0) {
		    look.y = next.y + krawly;
		    krawly = -krawly;
		}
		else
		    break; /* we have failed */
	    }
	    else
		success = true;
	}
	if (success) {
	    next = look;
	    if (idebug)
		proutn(cramlc(neither, next));
	}
	else
	    break; /* done early */
	
    }
    if (idebug)
	skip(1);
    /* Put commander in place within same quadrant */
    game.quad[com.x][com.y] = IHDOT;
    game.quad[next.x][next.y] = ienm;
    if (!same(next, com)) {
	/* it moved */
	game.ks[loccom] = next;
	game.kdist[loccom] = game.kavgd[loccom] = distance(game.sector, next);
	if (!damaged(DSRSENS) || game.condition == docked) {
	    proutn("***");
	    cramen(ienm);
	    proutn(_(" from %s"), cramlc(sector, com));
	    if (game.kdist[loccom] < dist1)
		proutn(_(" advances to "));
	    else
		proutn(_(" retreats to "));
	    prout(cramlc(sector, next));
	}
    }
}

void moveklings(void) 
/* Klingon tactical movement */
{
    coord w; 
    int i;

    if (idebug)
	prout("== MOVCOM");

    // Figure out which Klingon is the commander (or Supercommander)
    //   and do move
    if (game.comhere) 
	for (i = 1; i <= game.nenhere; i++) {
	    w = game.ks[i];
	    if (game.quad[w.x][w.y] == IHC) {
		movebaddy(w, i, IHC);
		break;
	    }
	}
    if (game.ishere) 
	for (i = 1; i <= game.nenhere; i++) {
	    w = game.ks[i];
	    if (game.quad[w.x][w.y] == IHS) {
		movebaddy(w, i, IHS);
		break;
	    }
	}
    // if skill level is high, move other Klingons and Romulans too!
    // Move these last so they can base their actions on what the
    // commander(s) do.
    if (game.skill >= SKILL_EXPERT && (game.options & OPTION_MVBADDY)) 
	for (i = 1; i <= game.nenhere; i++) {
	    w = game.ks[i];
	    if (game.quad[w.x][w.y] == IHK || game.quad[w.x][w.y] == IHR)
		movebaddy(w, i, game.quad[w.x][w.y]);
	}

    sortklings();
}

static bool movescom(coord iq, bool avoid) 
/* commander movement helper */
{
    int i;

    if (same(iq, game.quadrant) || !VALID_QUADRANT(iq.x, iq.y) ||
	game.state.galaxy[iq.x][iq.y].supernova ||
	game.state.galaxy[iq.x][iq.y].klingons > MAXKLQUAD-1) 
	return 1;
    if (avoid) {
	/* Avoid quadrants with bases if we want to avoid Enterprise */
	for (i = 1; i <= game.state.rembase; i++)
	    if (same(game.state.baseq[i], iq)) 
		return true;
    }
    if (game.justin && !game.iscate)
	return true;
    /* do the move */
    game.state.galaxy[game.state.kscmdr.x][game.state.kscmdr.y].klingons--;
    game.state.kscmdr = iq;
    game.state.galaxy[game.state.kscmdr.x][game.state.kscmdr.y].klingons++;
    if (game.ishere) {
	/* SC has scooted, Remove him from current quadrant */
	game.iscate=false;
	game.isatb=0;
	game.ishere = false;
	game.ientesc = false;
	unschedule(FSCDBAS);
	for (i = 1; i <= game.nenhere; i++) 
	    if (game.quad[game.ks[i].x][game.ks[i].y] == IHS)
		break;
	game.quad[game.ks[i].x][game.ks[i].y] = IHDOT;
	game.ks[i] = game.ks[game.nenhere];
	game.kdist[i] = game.kdist[game.nenhere];
	game.kavgd[i] = game.kavgd[game.nenhere];
	game.kpower[i] = game.kpower[game.nenhere];
	game.klhere--;
	game.nenhere--;
	if (game.condition!=docked)
	    newcnd();
	sortklings();
    }
    /* check for a helpful planet */
    for (i = 0; i < game.inplan; i++) {
	if (same(game.state.planets[i].w, game.state.kscmdr) &&
	    game.state.planets[i].crystals == present) {
	    /* destroy the planet */
	    game.state.planets[i].pclass = destroyed;
	    game.state.galaxy[game.state.kscmdr.x][game.state.kscmdr.y].planet = NOPLANET;
	    if (!damaged(DRADIO) || game.condition == docked) {
		announce();
		prout(_("Lt. Uhura-  \"Captain, Starfleet Intelligence reports"));
		proutn(_("   a planet in "));
		proutn(cramlc(quadrant, game.state.kscmdr));
		prout(_(" has been destroyed"));
		prout(_("   by the Super-commander.\""));
	    }
	    break;
	}
    }
    return false; /* looks good! */
}
			
void supercommander(void)
/* move the Super Commander */
{
    int i, i2, j, ideltax, ideltay, ifindit, iwhichb;
    coord iq, sc, ibq;
    int basetbl[BASEMAX+1];
    double bdist[BASEMAX+1];
    bool avoid;

    if (idebug)
	prout("== SUPERCOMMANDER");

    /* Decide on being active or passive */
    avoid = ((game.incom - game.state.remcom + game.inkling - game.state.remkl)/(game.state.date+0.01-game.indate) < 0.1*game.skill*(game.skill+1.0) ||
	    (game.state.date-game.indate) < 3.0);
    if (!game.iscate && avoid) {
	/* compute move away from Enterprise */
	ideltax = game.state.kscmdr.x-game.quadrant.x;
	ideltay = game.state.kscmdr.y-game.quadrant.y;
	if (sqrt(ideltax*(double)ideltax+ideltay*(double)ideltay) > 2.0) {
	    /* circulate in space */
	    ideltax = game.state.kscmdr.y-game.quadrant.y;
	    ideltay = game.quadrant.x-game.state.kscmdr.x;
	}
    }
    else {
	/* compute distances to starbases */
	if (game.state.rembase <= 0) {
	    /* nothing left to do */
	    unschedule(FSCMOVE);
	    return;
	}
	sc = game.state.kscmdr;
	for (i = 1; i <= game.state.rembase; i++) {
	    basetbl[i] = i;
	    bdist[i] = distance(game.state.baseq[i], sc);
	}
	if (game.state.rembase > 1) {
	    /* sort into nearest first order */
	    bool iswitch;
	    do {
		iswitch = false;
		for (i=1; i < game.state.rembase-1; i++) {
		    if (bdist[i] > bdist[i+1]) {
			int ti = basetbl[i];
			double t = bdist[i];
			bdist[i] = bdist[i+1];
			bdist[i+1] = t;
			basetbl[i] = basetbl[i+1];
			basetbl[i+1] =ti;
			iswitch = true;
		    }
		}
	    } while (iswitch);
	}
	/* look for nearest base without a commander, no Enterprise, and
	   without too many Klingons, and not already under attack. */
	ifindit = iwhichb = 0;

	for (i2 = 1; i2 <= game.state.rembase; i2++) {
	    i = basetbl[i2];	/* bug in original had it not finding nearest*/
	    ibq = game.state.baseq[i];
	    if (same(ibq, game.quadrant) || same(ibq, game.battle) ||
		game.state.galaxy[ibq.x][ibq.y].supernova ||
		game.state.galaxy[ibq.x][ibq.y].klingons > MAXKLQUAD-1) 
		continue;
	    // if there is a commander, and no other base is appropriate,
	    //   we will take the one with the commander
	    for (j = 1; j <= game.state.remcom; j++) {
		if (same(ibq, game.state.kcmdr[j]) && ifindit!= 2) {
		    ifindit = 2;
		    iwhichb = i;
		    break;
		}
	    }
	    if (j > game.state.remcom) { /* no commander -- use this one */
		ifindit = 1;
		iwhichb = i;
		break;
	    }
	}
	if (ifindit==0)
	    return; /* Nothing suitable -- wait until next time*/
	ibq = game.state.baseq[iwhichb];
	/* decide how to move toward base */
	ideltax = ibq.x - game.state.kscmdr.x;
	ideltay = ibq.y - game.state.kscmdr.y;
    }
    /* Maximum movement is 1 quadrant in either or both axis */
    if (ideltax > 1)
	ideltax = 1;
    if (ideltax < -1)
	ideltax = -1;
    if (ideltay > 1)
	ideltay = 1;
    if (ideltay < -1)
	ideltay = -1;

    /* try moving in both x and y directions */
    iq.x = game.state.kscmdr.x + ideltax;
    iq.y = game.state.kscmdr.y + ideltax;
    if (movescom(iq, avoid)) {
	/* failed -- try some other maneuvers */
	if (ideltax==0 || ideltay==0) {
	    /* attempt angle move */
	    if (ideltax != 0) {
		iq.y = game.state.kscmdr.y + 1;
		if (movescom(iq, avoid)) {
		    iq.y = game.state.kscmdr.y - 1;
		    movescom(iq, avoid);
		}
	    }
	    else {
		iq.x = game.state.kscmdr.x + 1;
		if (movescom(iq, avoid)) {
		    iq.x = game.state.kscmdr.x - 1;
		    movescom(iq, avoid);
		}
	    }
	}
	else {
	    /* try moving just in x or y */
	    iq.y = game.state.kscmdr.y;
	    if (movescom(iq, avoid)) {
		iq.y = game.state.kscmdr.y + ideltay;
		iq.x = game.state.kscmdr.x;
		movescom(iq, avoid);
	    }
	}
    }
    /* check for a base */
    if (game.state.rembase == 0) {
	unschedule(FSCMOVE);
    }
    else {
	for (i = 1; i <= game.state.rembase; i++) {
	    ibq = game.state.baseq[i];
	    if (same(ibq, game.state.kscmdr) && same(game.state.kscmdr, game.battle)) {
		/* attack the base */
		if (avoid)
		    return; /* no, don't attack base! */
		game.iseenit = false;
		game.isatb = 1;
		schedule(FSCDBAS, 1.0 +2.0*Rand());
		if (is_scheduled(FCDBAS)) 
		    postpone(FSCDBAS, scheduled(FCDBAS)-game.state.date);
		if (damaged(DRADIO) && game.condition != docked)
		    return; /* no warning */
		game.iseenit = true;
		announce();
		proutn(_("Lt. Uhura-  \"Captain, the starbase in "));
		proutn(cramlc(quadrant, game.state.kscmdr));
		skip(1);
		prout(_("   reports that it is under attack from the Klingon Super-commander."));
		proutn(_("   It can survive until stardate %d.\""),
		       (int)scheduled(FSCDBAS));
		if (!game.resting)
		    return;
		prout(_("Mr. Spock-  \"Captain, shall we cancel the rest period?\""));
		if (ja() == false)
		    return;
		game.resting = false;
		game.optime = 0.0; /* actually finished */
		return;
	    }
	}
    }
    /* Check for intelligence report */
    if (
	!idebug &&
	(Rand() > 0.2 ||
	 (damaged(DRADIO) && game.condition != docked) ||
	 !game.state.galaxy[game.state.kscmdr.x][game.state.kscmdr.y].charted))
	return;
    announce();
    prout(_("Lt. Uhura-  \"Captain, Starfleet Intelligence reports"));
    proutn(_("   the Super-commander is in "));
    proutn(cramlc(quadrant, game.state.kscmdr));
    prout(".\"");
    return;
}

void movetholian(void)
/* move the Tholian */
{
    int idx, idy, im, i;
    if (!game.ithere || game.justin)
	return;

    if (game.tholian.x == 1 && game.tholian.y == 1) {
	idx = 1; idy = QUADSIZE;
    }
    else if (game.tholian.x == 1 && game.tholian.y == QUADSIZE) {
	idx = QUADSIZE; idy = QUADSIZE;
    }
    else if (game.tholian.x == QUADSIZE && game.tholian.y == QUADSIZE) {
	idx = QUADSIZE; idy = 1;
    }
    else if (game.tholian.x == QUADSIZE && game.tholian.y == 1) {
	idx = 1; idy = 1;
    }
    else {
	/* something is wrong! */
	game.ithere = false;
	return;
    }

    /* do nothing if we are blocked */
    if (game.quad[idx][idy]!= IHDOT && game.quad[idx][idy]!= IHWEB)
	return;
    game.quad[game.tholian.x][game.tholian.y] = IHWEB;

    if (game.tholian.x != idx) {
	/* move in x axis */
	im = fabs((double)idx - game.tholian.x)/((double)idx - game.tholian.x);
	while (game.tholian.x != idx) {
	    game.tholian.x += im;
	    if (game.quad[game.tholian.x][game.tholian.y]==IHDOT)
		game.quad[game.tholian.x][game.tholian.y] = IHWEB;
	}
    }
    else if (game.tholian.y != idy) {
	/* move in y axis */
	im = fabs((double)idy - game.tholian.y)/((double)idy - game.tholian.y);
	while (game.tholian.y != idy) {
	    game.tholian.y += im;
	    if (game.quad[game.tholian.x][game.tholian.y]==IHDOT)
		game.quad[game.tholian.x][game.tholian.y] = IHWEB;
	}
    }
    game.quad[game.tholian.x][game.tholian.y] = IHT;
    game.ks[game.nenhere] = game.tholian;

    /* check to see if all holes plugged */
    for (i = 1; i <= QUADSIZE; i++) {
	if (game.quad[1][i]!=IHWEB && game.quad[1][i]!=IHT)
	    return;
	if (game.quad[QUADSIZE][i]!=IHWEB && game.quad[QUADSIZE][i]!=IHT)
	    return;
	if (game.quad[i][1]!=IHWEB && game.quad[i][1]!=IHT)
	    return;
	if (game.quad[i][QUADSIZE]!=IHWEB && game.quad[i][QUADSIZE]!=IHT)
	    return;
    }
    /* All plugged up -- Tholian splits */
    game.quad[game.tholian.x][game.tholian.y]=IHWEB;
    dropin(IHBLANK);
    crmena(true, IHT, sector, game.tholian);
    prout(_(" completes web."));
    game.ithere = false;
    game.nenhere--;
    return;
}
