/***************************************************************************
 *
 * PROJECT: The Dark Mod
 * $Revision$
 * $Date$
 * $Author$
 *
 ***************************************************************************/
/******************************************************************************/
/*                                                                            */
/*         Dark Mod Sound Propagation (C) by Chris Sarantos in USA 2005		  */
/*                          All rights reserved                               */
/*                                                                            */
/******************************************************************************/

/******************************************************************************
*
* DESCRIPTION: Sound propagation class for propagating suspicious sounds to AI
* during gameplay.  Friend class to CsndPropLoader.
*
*****************************************************************************/

#pragma hdrstop

#include "../game/game_local.h"

static bool init_version = FileVersionList("$Id$", init_version);

#pragma warning(disable : 4996)

#include "sndPropLoader.h"
#include "sndProp.h"
#include "MatrixSq.h"
#include "DarkModGlobals.h"
#include "Relations.h"
#include "../game/ai/ai.h"

// NOTES:
// ALL LOSSES ARE POSITIVE (ie, loss of +10dB subtracts 10dB from vol)

// ALL INITIAL VOLUMES ARE SWL [dB] (power level of the source, 10*log(power/1E-12 Watts))

// TODO: Add volInit to propParms instead of continually passing it as an argument

const float s_DOOM_TO_METERS = 0.0254f;					// doom to meters
const float s_METERS_TO_DOOM = (1.0f/DOOM_TO_METERS);	// meters to doom

/**
* Max number of areas to flood when doing wavefront expansion
*
* When the expansion goes above this number, it is terminated
*
* TODO: Read this from soundprop def file!
**/
const int s_MAX_FLOODNODES = 200;

/**
* Volume ( SPL [dB] ) threshold after which the sound stops propagating
* 
* Should correspond to the absolute lowest volume we want AI to be
* able to detect
*
* TODO: Read this from soundprop def file!
**/
const float s_MIN_AUD_THRESH = 15;

/**
* Max number of expansion nodes within which to use detailed path minimization
* (That is, trace the path back from AI thru the portals to find the optimum
*  points on the portal surface the path travels thru).
*
* TODO: Read this from soundprop def file!
**/
const float s_MAX_DETAILNODES = 3;


/**
* 1/log(10), useful for change of base between log and log10
**/
const float s_invLog10 = 0.434294482f;


/**************************************************
* BEGIN CsndProp Implementation
***************************************************/

CsndProp::CsndProp ( void )
{
	m_bLoadSuccess = false;
	m_bDefaultSpherical = false;

	m_EventAreas = NULL;
	m_PopAreas = NULL;
	m_sndAreas = NULL;
	m_PortData = NULL;

	m_numAreas = 0;
	m_numPortals = 0;

	m_TimeStampProp = 0;
	m_TimeStampPortLoss = 0;
}

void CsndProp::Clear( void )
{
	SPortEvent *pPortEv;

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Clearing sound prop gameplay object.\r");

	m_AreaPropsG.Clear();

	m_bLoadSuccess = false;
	m_bDefaultSpherical = false;

	if( m_EventAreas != NULL )
	{
		// delete portal event data array
		for( int i=0; i < m_numAreas; i++ )
		{
			pPortEv = m_EventAreas[i].PortalDat;
			if( pPortEv != NULL )
				delete[] pPortEv;
		}

		delete[] m_EventAreas;
		m_EventAreas = NULL;
	}

	if( m_PopAreas != NULL )
	{
		for( int i=0; i < m_numAreas; i++ )
		{
			m_PopAreas[i].AIContents.Clear();
			m_PopAreas[i].VisitedPorts.Clear();
		}

		delete[] m_PopAreas;
		m_PopAreas = NULL;
	}

	// delete m_sndAreas and m_PortData
	DestroyAreasData();
}

CsndProp::~CsndProp ( void )
{
	Clear();
}

void CsndProp::Save(idSaveGame *savefile) const
{
	// Pass the call to the base class first
	CsndPropBase::Save(savefile);

	savefile->WriteInt(m_TimeStampProp);
	savefile->WriteInt(m_TimeStampPortLoss);

	savefile->WriteInt(m_PopAreasInd.Num());
	for (int i = 0; i < m_PopAreasInd.Num(); i++)
	{
		savefile->WriteInt(m_PopAreasInd[i]);
	}

	for (int i = 0; i < m_numAreas; i++)
	{
		savefile->WriteInt(m_PopAreas[i].addedTime);
		savefile->WriteBool(m_PopAreas[i].bVisited);

		savefile->WriteInt(m_PopAreas[i].AIContents.Num());
		for (int j = 0; j < m_PopAreas[i].AIContents.Num(); j++)
		{
			m_PopAreas[i].AIContents[j].Save(savefile);
		}

		savefile->WriteInt(m_PopAreas[i].VisitedPorts.Num());
		for (int j = 0; j < m_PopAreas[i].VisitedPorts.Num(); j++)
		{
			savefile->WriteInt(m_PopAreas[i].VisitedPorts[j]);
		}
	}

	for (int i = 0; i < m_numAreas; i++)
	{
		savefile->WriteBool(m_EventAreas[i].bVisited);

		for (int portal = 0; portal < m_sndAreas[i].numPortals; portal++)
		{
			savefile->WriteFloat(m_EventAreas[i].PortalDat[portal].Loss);
			savefile->WriteFloat(m_EventAreas[i].PortalDat[portal].Dist);
			savefile->WriteFloat(m_EventAreas[i].PortalDat[portal].Att);
			savefile->WriteInt(m_EventAreas[i].PortalDat[portal].Floods);

			// Don't save ThisPort, it's just pointing at m_sndAreas

			// greebo: TODO: How to save PrevPort?
		}
	}
}

void CsndProp::Restore(idRestoreGame *savefile)
{
	int num;

	// Pass the call to the base class first
	CsndPropBase::Restore(savefile);

	savefile->ReadInt(m_TimeStampProp);
	savefile->ReadInt(m_TimeStampPortLoss);

	m_PopAreasInd.Clear();
	savefile->ReadInt(num);
	m_PopAreasInd.SetNum(num);
	for (int i = 0; i < num; i++)
	{
		savefile->ReadInt(m_PopAreasInd[i]);
	}

	m_PopAreas = new SPopArea[m_numAreas];
	for (int i = 0; i < m_numAreas; i++)
	{
		savefile->ReadInt(m_PopAreas[i].addedTime);
		savefile->ReadBool(m_PopAreas[i].bVisited);

		savefile->ReadInt(num);
		m_PopAreas[i].AIContents.Clear();
		for (int j = 0; j < num; j++)
		{
			idEntityPtr<idAI> ai;
			ai.Restore(savefile);
			m_PopAreas[i].AIContents.Append(ai);
		}

		savefile->ReadInt(num);
		m_PopAreas[i].VisitedPorts.Clear();
		m_PopAreas[i].VisitedPorts.SetNum(num);
		for (int j = 0; j < num; j++)
		{
			savefile->ReadInt(m_PopAreas[i].VisitedPorts[j]);
		}
	}

	m_EventAreas = new SEventArea[m_numAreas];
	for (int i = 0; i < m_numAreas; i++)
	{
		savefile->ReadBool(m_EventAreas[i].bVisited);

		m_EventAreas[i].PortalDat = new SPortEvent[m_sndAreas[i].numPortals];

		for (int portal = 0; portal < m_sndAreas[i].numPortals; portal++)
		{
			savefile->ReadFloat(m_EventAreas[i].PortalDat[portal].Loss);
			savefile->ReadFloat(m_EventAreas[i].PortalDat[portal].Dist);
			savefile->ReadFloat(m_EventAreas[i].PortalDat[portal].Att);
			savefile->ReadInt(m_EventAreas[i].PortalDat[portal].Floods);

			// Restore the ThisPort pointer, it's just pointing at m_sndAreas
			m_EventAreas[i].PortalDat[portal].ThisPort = &m_sndAreas[i].portals[portal];

			// greebo: TODO: How to restore PrevPort?
		}
	}
}

void CsndProp::SetupFromLoader( const CsndPropLoader *in )
{
	SAreaProp	defaultArea;
	int			tempint(0);
	int			numPorts;
	SEventArea *pEvArea;

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Setting up soundprop gameplay object\r");

	Clear();

	m_SndGlobals = in->m_SndGlobals;

	if( !in->m_bLoadSuccess )
	{
		// setup the default sound prop object for failed loads
		DM_LOG(LC_SOUND, LT_WARNING)LOGSTRING("SndPropLoader failed to load from the .spr file.\r");
		DM_LOG(LC_SOUND, LT_WARNING)LOGSTRING("SndProp is using default (simple, single area) setup\r");

		//TODO: Uncomment these when soundprop from file is fully implemented
		//gameLocal.Warning("[DM SPR] SndPropLoader failed to load from the .spr file.");
		//gameLocal.Warning("[DM SPR] "SndProp is the using default (simple, single area) setup.");

		//TODO : Need better default behavior for bad file, this isn't going to work
		defaultArea.area = 0;
		defaultArea.LossMult = 1.0 * m_SndGlobals.kappa0;
		defaultArea.VolMod = 0.0;
		defaultArea.DataEntered = false;

		m_AreaPropsG.Append( defaultArea );
		m_AreaPropsG.Condense();

		goto Quit;
	}

	m_bLoadSuccess = true;

	m_numAreas = in->m_numAreas;
	m_numPortals = in->m_numPortals;

	// copy the connectivity database from sndPropLoader
	if( (m_sndAreas = new SsndArea[m_numAreas]) == NULL )
	{
		DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("Out of memory when copying area connectivity database to gameplay object\r");
		goto Quit;
	}


	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Attempting to copy m_PortData with %d portals\r", m_numPortals);
	// copy the handle-indexed portal data from sndPropLoader
	if( (m_PortData = new SPortData[m_numPortals]) == NULL )
	{
		DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("Out of memory when copying portal data array to gameplay object\r");
		goto Quit;
	}

	// copy the areas array, element by element
	for( int i=0; i < m_numAreas; i++ )
	{
		m_sndAreas[i].LossMult = in->m_sndAreas[i].LossMult;
		m_sndAreas[i].VolMod = in->m_sndAreas[i].VolMod;
		
		tempint = in->m_sndAreas[i].numPortals;
		m_sndAreas[i].numPortals = tempint;

		m_sndAreas[i].center = in->m_sndAreas[i].center;

		m_sndAreas[i].portals = new SsndPortal[ tempint ];
		for( int k=0; k < tempint; k++ )
			m_sndAreas[i].portals[k] = in->m_sndAreas[i].portals[k];

		m_sndAreas[i].portalDists = new CMatRUT<float>;

		// Copy the values
		*m_sndAreas[i].portalDists = *(in->m_sndAreas[i].portalDists);
	}


	// copy the portal data array, element by element
	for( int k=0; k < m_numPortals; k++ )
	{
		m_PortData[k].loss = in->m_PortData[k].loss;
		
		m_PortData[k].Areas[0] = in->m_PortData[k].Areas[0];
		m_PortData[k].Areas[1] = in->m_PortData[k].Areas[1];
		m_PortData[k].LocalIndex[0] = in->m_PortData[k].LocalIndex[0];
		m_PortData[k].LocalIndex[1] = in->m_PortData[k].LocalIndex[1];
	}

	m_bDefaultSpherical = in->m_bDefaultSpherical;
	m_AreaPropsG = in->m_AreaPropsG;

	// initialize Event Areas
	if( (m_EventAreas = new SEventArea[m_numAreas]) == NULL )
	{
		DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("Out of memory when initializing m_EventAreas\r");
		goto Quit;
	}

	// initialize Populated Areas
	if( (m_PopAreas = new SPopArea[m_numAreas]) == NULL )
	{
		DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("Out of memory when initializing m_PopAreas\r");
		goto Quit;
	}

	// Initialize the timestamp in Populated Areas

	for ( int k=0; k<m_numAreas; k++ )
	{
		m_PopAreas[k].addedTime = 0;
	}
	
	// initialize portal loss arrays within Event Areas
	for( int j=0; j<m_numAreas; j++ )
	{
		pEvArea = &m_EventAreas[j];

		pEvArea->bVisited = false;

		numPorts = m_sndAreas[j].numPortals;

		if( (pEvArea->PortalDat = new SPortEvent[ numPorts ])
			== NULL )
		{
			DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("Out of memory when initializing portal data array for area %d in m_EventAreas\r", j);
			goto Quit;
		}

		// point the event portals to the m_sndAreas portals
		for( int l=0; l<numPorts; l++ )
		{
			SPortEvent *pEvPtr = &pEvArea->PortalDat[l];
			pEvPtr->ThisPort = &m_sndAreas[j].portals[l];
		}
	}

Quit:
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Soundprop gameplay object finished loading\r");
	return;
}

// NOTE: Propagate does not call CheckSound.  CheckSound should be called before
//	calling Propagate, in order to make sure the sound exists somewhere.

void CsndProp::Propagate 
	( float volMod, float durMod, const idStr& sndName,
	 idVec3 origin, idEntity *maker,
	 USprFlags *addFlags )

{
	bool bValidTeam(false),
		bExpandFinished(false);
	int			mteam;
	float		range;
	
	UTeamMask	tmask, compMask;
	
	idBounds	envBounds(origin);
	idAI				*testAI;
	idList<idEntity *>	validTypeEnts, validEnts;
	SPopArea			*pPopArea;

	idTimer timer_Prop;
	timer_Prop.Clear();
	timer_Prop.Start();

	m_TimeStampProp= gameLocal.time;

	if( cv_spr_debug.GetBool() )
	{
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("PROPAGATING: From entity %s, sound \"%s\", volume modifier %f, duration modifier %f \r", maker->name.c_str(), sndName.c_str(), volMod, durMod );
		gameLocal.Printf("PROPAGATING: From entity %s, sound \"%s\", volume modifier %f, duration modifier %f \n", maker->name.c_str(), sndName.c_str(), volMod, durMod );
	}

	// clear the old populated areas list
	m_PopAreasInd.Clear();

	// initialize the comparison team mask
	compMask.m_field = 0;
	
	// find the dict def for the specific sound
	const idDict* parms = gameLocal.FindEntityDefDict( va("sprGS_%s", sndName.c_str() ), false );

	// redundancy, this is already checked in CheckSound()
	if (!parms) return;

	float vol0 = parms->GetFloat("vol","0") + volMod;

	// add the area-specific volMod, if we're in an area
	int areaNum = gameRenderWorld->PointInArea(origin);
	vol0 += (areaNum >= 0) ? m_AreaPropsG[areaNum].VolMod : 0;

	// scale the volume by some amount that is be a cvar for now for tweaking
	// later we will put a permananet value in the def for globals->Vol
	vol0 += cv_ai_sndvol.GetFloat();

	if (cv_moveable_collision.GetBool() && maker->IsType(idMoveable::Type))
	{
		gameRenderWorld->DrawText( va("PropVol: %f", vol0), maker->GetPhysics()->GetOrigin(), 0.25f, colorGreen, gameLocal.GetLocalPlayer()->viewAngles.ToMat3(), 1, 100 * gameLocal.msec );
	}

	SSprParms propParms;
	propParms.name = sndName;
	propParms.alertFactor = parms->GetFloat("alert_factor","1");
	propParms.alertMax = parms->GetFloat("alert_max","30");

	// set team alert and propagation flags from the parms
	SetupParms( parms, &propParms, addFlags, &tmask );

	propParms.duration *= durMod;
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Found modified duration %f\r", propParms.duration);
	propParms.maker = maker;
	propParms.makerAI = (maker->IsType(idAI::Type)) ? static_cast<idAI*>(maker) : NULL;
	propParms.origin = origin;

	// For objects (non-actors) the team will be set to -1
	mteam = (maker->IsType(idActor::Type)) ? static_cast<idActor*>(maker)->team : -1;

	// Calculate the range, assuming peceived loudness of a sound doubles every 7 dB
	// (we want to overestimate a bit.  With the current settings, cutoff for a footstep
	// at 50dB is ~15 meters ( ~45 ft )

	// keep in mind that due to FOV compression, visual distances in FPS look shorter
	// than they actually are.

	range = pow(2.0f, ((vol0 - m_SndGlobals.MaxRangeCalVol) / 7.0f) ) * m_SndGlobals.MaxRange * s_METERS_TO_DOOM;

	// Debug drawing of the range
	if (cv_spr_radius_show.GetBool()) 
	{
		gameRenderWorld->DebugCircle(colorWhite, origin, idVec3(0,0,1), range, 100, 1000);
	}

	idBounds bounds(origin);
	bounds.ExpandSelf(range);

	// get a list of all ents with type idAI's or Listeners
	
	int count = 0;

	for (idAI* ai = gameLocal.spawnedAI.Next(); ai != NULL; ai = ai->aiNode.Next())
	{
		// TODO: Put in Listeners later
		validTypeEnts.Append(ai);
		count++;
	}
	
	if( cv_spr_debug.GetBool() )
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Found %d ents with valid type for propagation\r", validTypeEnts.Num() );

	timer_Prop.Stop();
	DM_LOG(LC_SOUND, LT_INFO)LOGSTRING("Timer: Finished finding all AI entities, comptime=%lf [ms]\r", timer_Prop.Milliseconds() );
	timer_Prop.Start();
	// cull the list by testing distance and valid team flag

	for ( int i=0; i<validTypeEnts.Num(); i++ )
	{
		bValidTeam = false; 

		// TODO : Do something else in the case of Listeners, since they're not AI
		testAI = static_cast<idAI *>( validTypeEnts[i] );

		// do not propagate to dead or unconscious AI
		if( testAI->health <= 0 || testAI->IsKnockedOut() )
			continue;
		
		if( !bounds.ContainsPoint( testAI->GetEyePosition() ) ) 
		{
			if( cv_spr_debug.GetBool() )
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI %s is not within propagation cutoff range %f\r", testAI->name.c_str(), range );
			continue;
		}

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI %s is within propagation cutoff range %f\r", testAI->name.c_str(), range );

		if( mteam == -1 )
		{
			// for now, inanimate objects alert everyone
			bValidTeam = true;
			if( cv_spr_debug.GetBool() )
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Sound was propagated from inanimate object: Alerts all teams\r" );
		}
		else
		{
			compMask.m_bits.same = ( testAI->team == mteam );
			compMask.m_bits.friendly = gameLocal.m_RelationsManager->IsFriend( testAI->team, mteam );
			compMask.m_bits.neutral = gameLocal.m_RelationsManager->IsNeutral( testAI->team, mteam );
			compMask.m_bits.enemy = gameLocal.m_RelationsManager->IsEnemy( testAI->team, mteam );

			// do the comparison
			if ( tmask.m_field & compMask.m_field )
			{
				bValidTeam = true;
				if( cv_spr_debug.GetBool() )
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI %s has a valid team for soundprop\r", testAI->name.c_str() );
			}
		}

		// TODO : Add another else if for the case of Listeners
		
		// don't alert the AI that caused the sound
		if( bValidTeam && testAI != maker )
		{
			if( cv_spr_debug.GetBool() )
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Found a valid propagation target: %s\r", testAI->name.c_str() );
			validEnts.Append( validTypeEnts[i] );
			continue;
		}
		if( cv_spr_debug.GetBool() )
			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI %s does not have a valid team for propagation\r", testAI->name.c_str() );

	}

	timer_Prop.Stop();
	DM_LOG(LC_SOUND, LT_INFO)LOGSTRING("Timer: Finished culling AI list, comptime=%lf [ms]\r", timer_Prop.Milliseconds() );
	timer_Prop.Start();

	/* handle environmental sounds here

	envBounds = bounds;
	envBounds.Expand( s_MAX_ENV_SNDRANGE * s_METERS_TO_DOOM);

	envBounds -= bounds;

	numEnt = gameLocal.clip.EntitiesTouchingBounds( envBounds, -1, inrangeEnts2, MAX_ENTS ); 

	for( int j =0; j < numEnt; j++)
	{
		// if the entities are in the env. sound hash
		// add them to the list of env. sounds to check for this propagation
	}
	*/

	// Don't bother propagation if no one is in range
	if (validEnts.Num() == 0) return;

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Beginning propagation to %d targets\r", validEnts.Num() );


	// ======================== BEGIN WAVEFRONT EXPANSION ===================

	// Populate the AI lists in the m_PopAreas array, use timestamp method to check if it's the first visit
	
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Filling populated areas array with AI\r" );
	for(int j = 0; j < validEnts.Num(); j++)
	{
		int AIAreaNum = gameRenderWorld->PointInArea( validEnts[j]->GetPhysics()->GetOrigin() );
		
		//Sometimes PointInArea returns -1, don't know why
		if (AIAreaNum < 0)
			continue;

		pPopArea = &m_PopAreas[AIAreaNum];
		
		if( pPopArea == NULL )
			continue;

		//DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("TimeStamp Debug: Area timestamp %d, new timestamp %d \r", pPopArea->addedTime, m_TimeStampProp);

		// check if this is the first update to the pop. area in this propagation
		if( pPopArea->addedTime != m_TimeStampProp )
		{
			//update the timestamp
			pPopArea->addedTime = m_TimeStampProp;

			pPopArea->bVisited = false;
			pPopArea->AIContents.Clear();
			pPopArea->VisitedPorts.Clear();

			// add the first AI to the contents list
			idEntityPtr<idAI> ai;
			ai = static_cast<idAI*>(validEnts[j]);
			pPopArea->AIContents.Append(ai);

			// append the index of this area to the popAreasInd list for later processing
			m_PopAreasInd.Append( AIAreaNum );
		}
		else
		{
			// This area has already been updated in this propagation, just add the next AI
			idEntityPtr<idAI> ai;
			ai = static_cast<idAI*>(validEnts[j]);
			pPopArea->AIContents.Append(ai);
		}
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Processed AI %s in area %d\r", validEnts[j]->name.c_str(), AIAreaNum );
	}
	
	timer_Prop.Stop();
	DM_LOG(LC_SOUND, LT_INFO)LOGSTRING("Timer: Finished filling populated areas, comptime=%lf [ms]\r", timer_Prop.Milliseconds() );
	timer_Prop.Start();

	bExpandFinished = ExpandWave( vol0, origin );

	//TODO: If bExpandFinished == false, either fake propagation or
	// delay further expansion until later frame
	if(bExpandFinished == false)
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Expansion was stopped when max node number %d was exceeded, or propagation was aborted\r", s_MAX_FLOODNODES );

	timer_Prop.Stop();
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Expansion done, processing AI\r" );
	DM_LOG(LC_SOUND, LT_INFO)LOGSTRING("Timer: COMPTIME=%lf [ms]\r", timer_Prop.Milliseconds() );
	timer_Prop.Start();

	ProcessPopulated( vol0, origin, &propParms );

	timer_Prop.Stop();
	DM_LOG(LC_SOUND, LT_INFO)LOGSTRING("Total TIME for propagation: %lf [ms]\r", timer_Prop.Milliseconds() );
}

void CsndProp::SetupParms( const idDict *parms, SSprParms *propParms, USprFlags *addFlags, UTeamMask *tmask )
{
	USprFlags tempflags;
	
	tempflags.m_field = 0;
	tmask->m_field = 0;
	
	DM_LOG(LC_SOUND,LT_DEBUG)LOGSTRING("Parsing team alert and propagation flags from propagated_sounds.def\r");
	
	// note: by default, if the key is not found, GetBool returns false
	tempflags.m_bits.same = parms->GetBool("alert_same");
	tempflags.m_bits.friendly = parms->GetBool("alert_friend");
	tempflags.m_bits.neutral = parms->GetBool("alert_neutral");
	tempflags.m_bits.enemy = parms->GetBool("alert_enemy", "1");

	tempflags.m_bits.omni_dir = parms->GetBool("omnidir");
	tempflags.m_bits.unique_loc = parms->GetBool("unique_loc");
	tempflags.m_bits.urgent = parms->GetBool("urgent");
	tempflags.m_bits.global_vol = parms->GetBool("global_vol");
	tempflags.m_bits.check_touched = parms->GetBool("check_touched");

	if( addFlags )
	{
		tempflags.m_field = tempflags.m_field | addFlags->m_field;
		if( cv_spr_debug.GetBool() )
			DM_LOG(LC_SOUND,LT_DEBUG)LOGSTRING("Added additional sound propagation flags from local sound \r");
	}
	
	// set the team mask from the sprflags
	tmask->m_bits.same = tempflags.m_bits.same;
	tmask->m_bits.friendly = tempflags.m_bits.friendly;
	tmask->m_bits.neutral = tempflags.m_bits.neutral;
	tmask->m_bits.enemy = tempflags.m_bits.enemy;

	// copy flags to parms
	propParms->flags = tempflags;

	// setup other parms
	propParms->duration = parms->GetFloat("dur","200");
	propParms->frequency = parms->GetInt("freq","-1");
	propParms->bandwidth = parms->GetFloat("width", "-1");
	
	if( cv_spr_debug.GetBool() )
	{
		DM_LOG(LC_SOUND,LT_DEBUG)LOGSTRING("Finished transfering sound prop parms\r");
	}
}

bool CsndProp::CheckSound( const char *sndNameGlobal, bool isEnv )
{
	const idDict *parms;
	bool returnval;

	if (isEnv)
		parms = gameLocal.FindEntityDefDict( va("sprGE_%s", sndNameGlobal ), false );
	else
		parms = gameLocal.FindEntityDefDict( va("sprGS_%s", sndNameGlobal ), false );

	if ( !parms )
	{
		// Don't log this, because it happens all the time.  Most sounds played with idEntity::StartSound are not propagated.
		//if( cv_spr_debug.GetBool() )
			//gameLocal.Warning("[Soundprop] Could not find sound def for sound \"%s\" Sound not propagated.", sndNameGlobal );
		//DM_LOG(LC_SOUND, LT_WARNING)LOGSTRING("Could not find sound def for sound \"%s\" Sound not propagated.\r", sndNameGlobal );
		returnval = false;
		goto Quit;
	}
	
	else
	{
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Found propagated sound \"%s\" in the def.\r", sndNameGlobal );
		returnval = true;
	}

Quit:
	return returnval;
}

bool CsndProp::ExpandWave(float volInit, idVec3 origin)
{
	bool				returnval;
	int					//popIndex(-1),
		floods(1), nodes(0), area, LocalPort;
	float				tempDist(0), tempAtt(1), tempLoss(0), AddedDist(0);
	idList<SExpQue>		NextAreas; // expansion queue
	idList<SExpQue>		AddedAreas; // temp storage for next expansion queue
	SExpQue				tempQEntry;
	SPortEvent			*pPortEv; // pointer to portal event data
	SPopArea			*pPopArea; // pointer to populated area data

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Starting wavefront expansion\r" );

	// clear the visited settings on m_EventAreas from previous propagations
	for(int i=0; i < m_numAreas; i++)
		m_EventAreas[i].bVisited = false;

	NextAreas.Clear();
	AddedAreas.Clear();

	
	// ======================== Handle the initial area =========================

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Processing initial area\r" );

	int initArea = gameRenderWorld->PointInArea( origin );
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Sound origin is in portal area: %d\r", initArea );
	if( initArea == -1 )
	{
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Sound origin is outside the map, aborting propagation.\r" );
		return false;
	}

	m_EventAreas[ initArea ].bVisited = true;

	// Update m_PopAreas to show that the area has been visited
	m_PopAreas[ initArea ].bVisited = true;

	// array index pointers to save on calculation
	SsndArea *pSndAreas = &m_sndAreas[ initArea ];
	SEventArea *pEventAreas = &m_EventAreas[ initArea ];

	// calculate initial portal losses from the sound origin point
	for( int i2=0; i2 < pSndAreas->numPortals; i2++)
	{
		idVec3 portalCoord = pSndAreas->portals[i2].center;
		tempDist = (origin - portalCoord).LengthFast() * s_DOOM_TO_METERS;

		// calculate and set initial portal losses
		tempAtt = m_AreaPropsG[ initArea ].LossMult * tempDist;
		
		// add the door loss
		tempAtt += m_PortData[ pSndAreas->portals[i2].handle - 1 ].loss;

		// get the current loss
		tempLoss = m_SndGlobals.Falloff_Ind * s_invLog10*idMath::Log16(tempDist) + tempAtt + 8;

		pPortEv = &pEventAreas->PortalDat[i2];

		pPortEv->Loss = tempLoss;
		pPortEv->Dist = tempDist;
		pPortEv->Att = tempAtt;
		pPortEv->Floods = 1;
		pPortEv->PrevPort = NULL;

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Loss at portal %d is %f [dB]\r", i2, tempLoss);
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Dist at portal %d is %f [m]\r", i2, tempDist);


		// add the portal destination to flooding queue if the sound has
		//	not dropped below threshold at the portal
		if( (volInit - tempLoss) > s_MIN_AUD_THRESH )
		{
			tempQEntry.area = pSndAreas->portals[i2].to;
			tempQEntry.curDist = tempDist;
			tempQEntry.curAtt = tempAtt;
			tempQEntry.curLoss = tempLoss;
			tempQEntry.portalH = pSndAreas->portals[i2].handle;
			tempQEntry.PrevPort = NULL;

			NextAreas.Append( tempQEntry );
		}
		else
			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Wavefront intensity dropped below threshold at portal %d\r", i2);
	}

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Starting main loop\r" );
	
	
// done with initial area, begin main loop

	while( NextAreas.Num() > 0 && nodes < s_MAX_FLOODNODES )
	{
		floods++;

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Expansion loop, iteration %d\r", floods);

		AddedAreas.Clear();

		for(int j=0; j < NextAreas.Num(); j++)
		{
			nodes++;

			area = NextAreas[j].area;

			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Flooding area %d thru portal handle %d\r", area, NextAreas[j].portalH);

			// array index pointers to save on calculation
			pSndAreas = &m_sndAreas[ area ];
			pEventAreas = &m_EventAreas[ area ];
			pPopArea = &m_PopAreas[area];

			// find the local portal number in area for the portal handle
			int portHandle = NextAreas[j].portalH;

			SPortData *pPortData = &m_PortData[ portHandle - 1 ];
			if( pPortData->Areas[0] == area )
				LocalPort = pPortData->LocalIndex[0];
			else
				LocalPort = pPortData->LocalIndex[1];
			
			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Identified local portal index %d\r", LocalPort );

			pPortEv = &pEventAreas->PortalDat[ LocalPort ];

			// copy information from the portal's other side
			pPortEv->Dist = NextAreas[j].curDist;
			pPortEv->Att = NextAreas[j].curAtt;
			pPortEv->Loss = NextAreas[j].curLoss;
			pPortEv->Floods = floods - 1;
			pPortEv->PrevPort = NextAreas[j].PrevPort;


			// Updated the Populated Areas to show that it's been visited
			// Only do this for populated areas that matter (ie, they've been updated
			//	on this propagation
			
			if ( pPopArea->addedTime == m_TimeStampProp )
			{
				pPopArea->bVisited = true;
				// note the portal flooded in on for later processing
				pPopArea->VisitedPorts.AddUnique( LocalPort );
			}

			// Flood to portals in this area
			for( int i=0; i < pSndAreas->numPortals; i++)
			{
				// do not flood back thru same portal we came in
				if( LocalPort == i)
					continue;

				// set up the portal event pointer
				pPortEv = &pEventAreas->PortalDat[i];

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Calculating loss from portal %d to portal %d in area %d\r", LocalPort, i, area);
		
				// Obtain loss at this portal and store in temp var
				tempDist = NextAreas[j].curDist;
				AddedDist = pSndAreas->portalDists->GetRev( LocalPort, i );
				tempDist += AddedDist;

				tempAtt = NextAreas[j].curAtt;
				tempAtt += AddedDist * m_AreaPropsG[ area ].LossMult;
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Total distance now %f\r", tempDist );
				
				// add any specific loss on the portal
				tempAtt += m_PortData[ pSndAreas->portals[i].handle - 1 ].loss;

				tempLoss = m_SndGlobals.Falloff_Ind * s_invLog10*idMath::Log16(tempDist) + tempAtt + 8;

				// check if we've visited the area, and do not add destination area 
				//	if loss is greater this time
				if( pEventAreas->bVisited 
					&& tempLoss >= pPortEv->Loss )
				{
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Cancelling flood thru portal %d in previously visited area %d\r", i, area);
					continue;
				}

				if( ( volInit - tempLoss ) < s_MIN_AUD_THRESH )
				{
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Wavefront intensity dropped below abs min audibility at portal %d in area %d\r", i, area);
					continue;
				}

				// path has been determined to be minimal loss, above cutoff intensity
				// store the loss value

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Further expansion valid thru portal %d in area %d\r", i, area);
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Set loss at portal %d to %f [dB]\r", i, tempLoss);
				
				pPortEv->Loss = tempLoss;
				pPortEv->Dist = tempDist;
				pPortEv->Att = tempAtt;
				pPortEv->Floods = floods;
				pPortEv->PrevPort = &pEventAreas->PortalDat[ LocalPort ];

				// add the portal destination to flooding queue
				tempQEntry.area = pSndAreas->portals[i].to;
				tempQEntry.curDist = tempDist;
				tempQEntry.curAtt = tempAtt;
				tempQEntry.curLoss = tempLoss;
				tempQEntry.portalH = pSndAreas->portals[i].handle;
				tempQEntry.PrevPort = pPortEv->PrevPort;

				AddedAreas.Append( tempQEntry );
			
			} // end portal flood loop

			m_EventAreas[area].bVisited = true;
		} // end area flood loop

		// create the next expansion queue
		NextAreas = AddedAreas;

	} // end main loop

	// return true if the expansion died out naturally rather than being stopped
	returnval = ( !NextAreas.Num() );

	return returnval;
} // end function

void CsndProp::ProcessPopulated( float volInit, idVec3 origin, SSprParms *propParms )
{
	float LeastLoss, TestLoss, tempDist, tempAtt, tempLoss;
	int LoudPort(0), portNum;
	idVec3 testLoc;
	SPortEvent *pPortEv;
	SPopArea *pPopArea;
	idList<idVec3> showPoints;
	
	int initArea = gameRenderWorld->PointInArea( origin );

	for( int i=0; i < m_PopAreasInd.Num(); i++ )
	{
		int area = m_PopAreasInd[i];

		pPopArea = &m_PopAreas[area];

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Processing pop. area %d\r", area);
		
		// Special case: AI area = initial area - no portal flooded in on in this case
		if( area == initArea )
		{
			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Special case: AI in initial area %d\r", area);

			propParms->bSameArea = true;
			propParms->direction = origin;

			for(int j=0; j < pPopArea->AIContents.Num(); j++)
			{
				idAI* ai = pPopArea->AIContents[j].GetEntity();
				
				if (ai == NULL)
					continue;

				tempDist = (origin - ai->GetEyePosition()).LengthFast() * s_DOOM_TO_METERS;
				tempAtt = tempDist * m_AreaPropsG[ area ].LossMult;
				tempLoss = m_SndGlobals.Falloff_Ind * s_invLog10*idMath::Log16(tempDist) + tempAtt + 8;

				propParms->propVol = volInit - tempLoss;

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Messaging AI %s in (source area) area %d\r", ai->name.c_str(), area);
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Dist to AI: %f [m], Propagated volume found to be %f [dB]\r", tempDist, propParms->propVol);
				
				ProcessAI( ai, origin, propParms );

				// draw debug lines if show soundprop cvar is set
				if( cv_spr_show.GetBool() )
				{
					showPoints.Clear();
					showPoints.Append( ai->GetEyePosition() );
					showPoints.Append( propParms->origin );
					DrawLines(showPoints);
				}
			}
		}
		// Normal propagation to a surrounding area
		else if ( pPopArea->bVisited == true )
		{
			propParms->bSameArea = false;

			// figure out the least loss portal
			// May be different for each AI (esp. in large rooms)
			// TODO OPTIMIZATION: Don't do this extra loop for each AI if 
			//		we only visited one portal in the area

			for( int aiNum = 0; aiNum < pPopArea->AIContents.Num(); aiNum++ )
			{
				idAI* ai = pPopArea->AIContents[ aiNum ].GetEntity();

				if (ai == NULL)
				{
					DM_LOG(LC_SOUND, LT_WARNING)LOGSTRING("NULL AI pointer for AI %d in area %d\r", aiNum, area);
					continue;
				}

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Calculating least loss for AI %s in area %d\r", ai->name.c_str(), area);

				LeastLoss = idMath::INFINITY;

				for(int k=0; k < pPopArea->VisitedPorts.Num(); k++ )
				{	
					portNum = pPopArea->VisitedPorts[ k ];
					pPortEv = &m_EventAreas[area].PortalDat[ portNum ];

					//DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Calculating loss from portal %d, DEBUG k=%d, portsnum = %d\r", portNum, k, m_PopAreas[i].VisitedPorts.Num());

					testLoc = m_sndAreas[area].portals[portNum].center;

					tempDist = (testLoc - ai->GetEyePosition()).LengthFast() * s_DOOM_TO_METERS;
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI Calc: Distance to AI = %f [m]\r", tempDist);

					tempAtt = tempDist * m_AreaPropsG[ area ].LossMult;
					tempDist += pPortEv->Dist;
					tempAtt += pPortEv->Att;

					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI Calc: Portal %d has total Dist = %f [m]\r", portNum, tempDist);

					// add loss from portal to AI to total loss at portal
					TestLoss = m_SndGlobals.Falloff_Ind * s_invLog10*idMath::Log16(tempDist) + tempAtt + 8;
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("AI Calc: Portal %d has total Loss = %f [dB]\r", portNum, TestLoss);

					if( TestLoss < LeastLoss )
					{
						LeastLoss = TestLoss;
						LoudPort = portNum;
					}
				}
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Portal %d has least loss %f [dB]\r", LoudPort, LeastLoss );

				pPortEv = &m_EventAreas[area].PortalDat[ LoudPort ];
				propParms->floods = pPortEv->Floods;

				// Detailed Path Minimization: 

				// check if AI is within the flood range for detailed path minimization	
				if( pPortEv->Floods <= s_MAX_DETAILNODES )
				{
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Starting detailed path minimization for portal  %d\r", LoudPort );
					propParms->bDetailedPath = true;

					// call detailed path minimization, which writes results to propParms
					DetailedMin( ai, propParms, pPortEv, area, volInit ); 

					// message the AI
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Propagated volume found to be %f\r", propParms->propVol);
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Messaging AI %s in area %d\r", ai->name.c_str(), area);
					
					ProcessAI( ai, origin, propParms );
					
					continue;
				}

				propParms->bDetailedPath = false;
				propParms->direction = m_sndAreas[area].portals[ LoudPort ].center;
				propParms->propVol = volInit - LeastLoss;

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Propagated volume found to be %f\r", propParms->propVol);
				
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Messaging AI %s in area %d\r", ai->name.c_str(), area);
				ProcessAI( ai, origin, propParms );
			}
		}
		// Propagation was stopped before this area was reached
		else if ( pPopArea->bVisited == false )
		{
			// Do nothing for now
			// TODO: Keep track of these areas for delayed calculation?
		}
	}

	// greebo: We're done propagating, clear the message list of the issuing AI, if appropriate
	if (propParms->makerAI != NULL)
	{
		propParms->makerAI->ClearMessages();
	}
}

void CsndProp::ProcessAI(idAI* ai, idVec3 origin, SSprParms *propParms)
{
	if( ai == NULL ) return;

	// check AI hearing, get environmental noise, etc
	if( cv_spr_debug.GetBool() )
	{
		gameLocal.Printf("Propagated sound %s to AI %s, from origin %s : Propagated volume %f, Apparent origin of sound: %s \r", 
						  propParms->name.c_str(), ai->name.c_str(), origin.ToString(), propParms->propVol, propParms->direction.ToString() );

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Propagated sound %s to AI %s, from origin %s : Propagated volume %f, Apparent origin of sound: %s \r", 
											  propParms->name.c_str(), ai->name.c_str(), origin.ToString(), propParms->propVol, propParms->direction.ToString() );
	}

	if( cv_spr_show.GetBool() )
	{
		gameRenderWorld->DrawText( va("Volume: %.2f", propParms->propVol), 
			(ai->GetEyePosition() - ai->GetPhysics()->GetGravityNormal() * 65.0f), 0.25f, 
			colorGreen, gameLocal.GetLocalPlayer()->viewAngles.ToMat3(), 1, gameLocal.msec * 30);
	}

	// convert the SPL to loudness and store it in parms
	ai->SPLtoLoudness( propParms );

	if (ai->CheckHearing(propParms))
	{
		// TODO: Add env. sound masking check here
		// GetEnvNoise should check all the env. noises on the list we made, plus global ones
		
		// noiseVol = GetEnvNoise( &propParms, origin, AI->GetEyePosition() );
		float noise = 0;
		
		//message the AI
		ai->HearSound( propParms, noise, origin );
	}
}

/**
* CsndProp::OptSurfPoint
* PSUEDOCODE
*
* 1. Define the surface coordinates by taking vectors to two consecutive 
* 	points in the winding.
* 
* 2.	Obtain intersection point with winding plane.
* 2.A Test if this point is within the rectangle.  If so, we're done. (goto Quit)
* 
* 3. Obtain a1 and a2, the coordinates when line is resolved onto the axes
* 
* 4. Test sign of a1 and a2 to determine which edge they should intersect
* 	write the lower numbered point of the intersection edge to edgeNum
* 
* 
* 5. Find the point along the edge closest to point A
* -Find line that goes thru point isect, and is also perpendicular to the edge
* 
* 6. Return this point in world coordinates, we're done.
**/
idVec3 CsndProp::OptSurfPoint( idVec3 p1, idVec3 p2, const idWinding& wind, idVec3 WCenter )
{
	idVec3 line, u1, u2, v1, v2, edge, isect, lineSect;
	idVec3 returnVec, tempVec, pointA;
	idPlane WPlane;
	float lenV1, lenV2, lineU1, lineU2, Scale(0), frac;
	int edgeStart(0), edgeStop(0);

	// If the winding is not a rectangle, just return the center coordinate
	if( wind.GetNumPoints() != 4 )
	{
		returnVec = WCenter;
		goto Quit;
	}

	// Want to find a point on the portal rectangle closest to this line:
	line = p2 - p1;

	// define the portal coordinates and extent of the two corners
	u1 = (wind[0].ToVec3() - WCenter);
	u2 = (wind[1].ToVec3() - WCenter);
	u1.NormalizeFast();
	u2.NormalizeFast();

	// define other coordinates going to midpoint between two points (useful to see if point is on portal)
	v1 = (wind[1].ToVec3() + wind[0].ToVec3()) / 2 - WCenter;
	v2 = (wind[2].ToVec3() + wind[1].ToVec3()) / 2 - WCenter;
	lenV1 = v1.LengthFast();
	lenV2 = v2.LengthFast();

	wind.GetPlane(WPlane);

	tempVec = p2-p1;
	tempVec.NormalizeFast();

	// find ray intersection point, in terms of p1 + (p2-p1)*Scale
	WPlane.RayIntersection( p1, tempVec, Scale );

	isect = p1 + Scale * tempVec;
	lineSect = isect - WCenter;

	if( cv_spr_show.GetBool() )
	{
		gameRenderWorld->DebugLine( colorRed, WCenter, (wind[1].ToVec3() + wind[0].ToVec3()) / 2, 3000);
		gameRenderWorld->DebugLine( colorRed, WCenter, (wind[2].ToVec3() + wind[1].ToVec3()) / 2, 3000);
		//gameRenderWorld->DebugLine( colorYellow, WCenter, isect, 3000);
	}

	// resolve into surface coordinates
	lineU1 = lineSect * u1;
	lineU2 = lineSect * u2;

	// If point is within the rectangular surface boundaries, we're done
	// Use the v axes (going to edge midpoints) to check if point is within rectangle
	if( fabs(lineSect * v1/lenV1) <= lenV1 && fabs( lineSect * v2/lenV2) <= lenV2 )
	{
//		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("MinSurf: Line itersects inside portal surface\r" );
//		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("v1/lenV1 = %f, v2/lenV2 = %f\r", fabs(lineSect * v1/lenV1)/lenV1, fabs( lineSect * v2/lenV2)/lenV2 );
		returnVec = isect;
		goto Quit;
	}

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("MinSurf: Line intersected outside of portal\r");
	// find the edge that the line intersects
	if( lineU1 >= 0 && lineU2 >= 0 )
	{
		edgeStart = 0;
		edgeStop = 1;
	}
	else if( lineU1 <= 0 && lineU2 >= 0 )
	{
		edgeStart = 1;
		edgeStop = 2;
	}
	else if( lineU1 <= 0 && lineU2 <= 0 )
	{
		edgeStart = 2;
		edgeStop = 3;
	}
	else if( lineU1 >= 0 && lineU2 <= 0 )
	{
		edgeStart = 3;
		edgeStop = 0;
	}

	pointA = wind[edgeStart].ToVec3();
	edge = wind[edgeStop].ToVec3() - pointA;

	// Find the closest point on the edge to the isect point
	// This is the point we're looking for
	frac = ((isect - pointA) * edge )/ edge.LengthSqr();

	// check if the point is outside the actual edge, if not, set it to
	//	the appropriate endpoint.
	if( frac < 0 )
		returnVec = pointA;
	else if( frac > 1.0 )
		returnVec = pointA + edge;
	else
		returnVec = pointA + frac * edge;

Quit:
	return returnVec;
}

void CsndProp::DetailedMin( idAI* AI, SSprParms *propParms, SPortEvent *pPortEv, int AIArea, float volInit )
{
	idList<idVec3>		pathPoints; // pathpoints[0] = closest path point to the TARGET
	idList<SsndPortal*> PortPtrs; // pointers to the portals along the path
	idVec3				point, p1, p2, AIpos;
	int					floods, curArea;
	float				tempAtt, tempDist, totAtt, totDist, totLoss;
	SPortEvent			*pPortTest;
	SsndPortal			*pPort2nd;

	floods = pPortEv->Floods;
	pPortTest = pPortEv;

	AIpos = AI->GetEyePosition();
	p1 = AIpos;


	// first iteration, populate pathPoints going "backwards" from target to source
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Path min: Performing first iteration for %d floods\r", floods );
	
	for( int i = 0; i < floods; i++ )
	{
		if( pPortEv->ThisPort == NULL)
			DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("ERROR: pPortEv->ThisPort is NULL\r" );

		// calculate optimum point for this leg of the path
		point = OptSurfPoint( p1, propParms->origin, *pPortTest->ThisPort->winding, 
							  pPortTest->ThisPort->center );

		pathPoints.Append(point);
		PortPtrs.Append( pPortTest->ThisPort );

		p1 = point;
		pPortTest = pPortTest->PrevPort;
	}

	// check if we have enough floods to perform the 2nd iteration
	if( (floods - 2) < 0 )
	{
		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Path min: Skipping second iteration, not enough portals\r" );
		goto Quit;
	}

	// second iteration, going forwards from source to target
	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Path min: Performing second iteration\r" );


	p1 = propParms->origin;

	for( int k = 0; k < floods; k++ )
	{
		// recalculate the N-1th point using the Nth point and N-2th point on either side
		pPort2nd = PortPtrs[floods - k - 1];
		if( pPort2nd == NULL)
			DM_LOG(LC_SOUND, LT_ERROR)LOGSTRING("ERROR: pPort2nd is NULL\r" );

		// the last point tested must be the AI position
		if( (floods - k - 2) >= 0 )
			p2 = pathPoints[floods - k - 2];
		else
			p2 = AIpos;
		
		// DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("2nd iter: Finding optimum surface point\r" );
		point = OptSurfPoint( p1, p2, *pPort2nd->winding, pPort2nd->center );
		pathPoints[floods - k -1] = point;

		p1 = point;
	}

Quit:

// Now go through and calculate the new loss

	// get loss to 1st point on the path
	totDist = (AIpos - pathPoints[0]).LengthFast() * s_DOOM_TO_METERS;
	curArea = PortPtrs[0]->to;
	totAtt = totDist * m_AreaPropsG[ curArea ].LossMult;

	// add the rest of the loss from the path points
	for( int j = 0; j < (floods - 1); j++ )
	{
		tempDist = (pathPoints[j+1] - pathPoints[j]).LengthFast() * s_DOOM_TO_METERS;
		curArea = PortPtrs[j]->to;
		tempAtt = tempDist * m_AreaPropsG[ curArea ].LossMult;
		
		totDist += tempDist;
		totAtt += tempAtt;
	}

	// add the loss from the final path point to the source
	tempDist = ( pathPoints[floods - 1] - propParms->origin ).LengthFast() * s_DOOM_TO_METERS;
	curArea = PortPtrs[ floods - 1 ]->to;
	tempAtt = tempDist * m_AreaPropsG[ curArea ].LossMult;

	totDist += tempDist;
	totAtt += tempAtt;

	// Finally, convert to acoustic spreading + attenuation loss in dB
	totLoss = m_SndGlobals.Falloff_Ind * s_invLog10*idMath::Log16(totDist) + totAtt + 8;

	propParms->direction = pathPoints[0];
	propParms->propVol = volInit - totLoss;

	// draw debug lines if show soundprop cvar is set
	if( cv_spr_show.GetBool() )
	{
		pathPoints.Insert(AIpos, 0);
		pathPoints.Append(propParms->origin);
		DrawLines(pathPoints);
	}

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Detailed path minimization for AI %s finished\r", AI->name.c_str() );

	return;
}

void CsndProp::DrawLines(idList<idVec3>& pointlist)
{
	for (int i = 0; i < (pointlist.Num() - 1); i++)
	{
		gameRenderWorld->DebugLine( colorGreen, pointlist[i], pointlist[i+1], 3000);
	}
}

void CsndProp::SetPortalLoss( int handle, float value )
{
	CsndPropBase::SetPortalLoss( handle, value );

	// update the portal loss info timestamp
	m_TimeStampPortLoss = gameLocal.time;
}

bool CsndProp::ExpandWaveFast( float volInit, idVec3 origin, float MaxDist, int MaxFloods )
{
	bool				bDistLimit(false);
	int					//popIndex(-1),
		floods(1), nodes(0), area, LocalPort, FloodLimit;
	float				tempDist(0), tempAtt(1), AddedDist(0);
	idList<SExpQue>		NextAreas; // expansion queue
	idList<SExpQue>		AddedAreas; // temp storage for next expansion queue
	SExpQue				tempQEntry;
	SPortEvent			*pPortEv; // pointer to portal event data
	SPopArea			*pPopArea; // pointer to populated area data

	if( MaxDist != -1 )
		bDistLimit = true;

	if( MaxFloods == -1 )
		FloodLimit = s_MAX_FLOODNODES;
	else
		FloodLimit = MaxFloods;

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Starting wavefront expansion (fast), DistLimit = %f, NodeLimit = %d\r", MaxDist, FloodLimit );

	// clear the visited settings on m_EventAreas from previous propagations
	for(int i=0; i < m_numAreas; i++)
		m_EventAreas[i].bVisited = false;

	NextAreas.Clear();
	AddedAreas.Clear();

	
	// ======================== Handle the initial area =========================

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Processing initial area\r" );

	int initArea = gameRenderWorld->PointInArea( origin );

	m_EventAreas[ initArea ].bVisited = true;

	// Update m_PopAreas to show that the area has been visited
	m_PopAreas[ initArea ].bVisited = true;

	// array index pointers to save on calculation
	SsndArea *pSndAreas = &m_sndAreas[ initArea ];
	SEventArea *pEventAreas = &m_EventAreas[ initArea ];

	// calculate initial portal losses from the sound origin point
	for( int i2=0; i2 < pSndAreas->numPortals; i2++)
	{
		idVec3 portalCoord = pSndAreas->portals[i2].center;

		tempDist = (origin - portalCoord).LengthFast() * s_DOOM_TO_METERS;
		// calculate and set initial portal losses
		tempAtt = m_AreaPropsG[ initArea ].LossMult * tempDist;
		
		// add the door loss
		tempAtt += m_PortData[ pSndAreas->portals[i2].handle - 1 ].loss;

		pPortEv = &pEventAreas->PortalDat[i2];

		pPortEv->Dist = tempDist;
		pPortEv->Att = tempAtt;
		pPortEv->Floods = 1;
		pPortEv->PrevPort = NULL;

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Dist at portal %d is %f [m]\r", i2, tempDist);


		// add the portal destination to flooding queue if the sound has
		//	not dropped below threshold at the portal
		if( !bDistLimit || tempDist < MaxDist )
		{
			tempQEntry.area = pSndAreas->portals[i2].to;
			tempQEntry.curDist = tempDist;
			tempQEntry.curAtt = tempAtt;
			tempQEntry.portalH = pSndAreas->portals[i2].handle;
			tempQEntry.PrevPort = NULL;
			tempQEntry.curLoss = 0.0f; // greebo: Initialised to 0.0f to fix gcc warning

			NextAreas.Append( tempQEntry );
		}
		else
			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Distance rose above max distance at portal %d\r", i2);
	}

	DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Starting main loop\r" );
	
	
// done with initial area, begin main loop

	while( NextAreas.Num() > 0 && ( (floods < FloodLimit) ) )
	{
		floods++;

		DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Expansion loop, iteration %d\r", floods);

		AddedAreas.Clear();

		for(int j=0; j < NextAreas.Num(); j++)
		{
			nodes++;

			area = NextAreas[j].area;

			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Flooding area %d thru portal handle %d\r", area, NextAreas[j].portalH);

			// array index pointers to save on calculation
			pSndAreas = &m_sndAreas[ area ];
			pEventAreas = &m_EventAreas[ area ];
			pPopArea = &m_PopAreas[area];

			// find the local portal number in area for the portal handle
			int portHandle = NextAreas[j].portalH;

			SPortData *pPortData = &m_PortData[ portHandle - 1 ];
			if( pPortData->Areas[0] == area )
				LocalPort = pPortData->LocalIndex[0];
			else
				LocalPort = pPortData->LocalIndex[1];
			
			DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Identified local portal index %d\r", LocalPort );

			pPortEv = &pEventAreas->PortalDat[ LocalPort ];

			// copy information from the portal's other side
			pPortEv->Dist = NextAreas[j].curDist;
			pPortEv->Att = NextAreas[j].curAtt;
			pPortEv->Floods = floods - 1;
			pPortEv->PrevPort = NextAreas[j].PrevPort;


			// Updated the Populated Areas to show that it's been visited
			// Only do this for populated areas that matter (ie, they've been updated
			//	on this propagation

			if ( pPopArea->addedTime == m_TimeStampProp )
			{
				pPopArea->bVisited = true;
				// note the portal flooded in on for later processing
				pPopArea->VisitedPorts.Append( LocalPort );
			}

			// Flood to portals in this area
			for( int i=0; i < pSndAreas->numPortals; i++)
			{
				// do not flood back thru same portal we came in
				if( LocalPort == i)
					continue;

				// set up the portal event pointer
				pPortEv = &pEventAreas->PortalDat[i];

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Calculating loss from portal %d to portal %d in area %d\r", LocalPort, i, area);
		
				// Obtain loss at this portal and store in temp var
				tempDist = NextAreas[j].curDist;
				AddedDist = pSndAreas->portalDists->GetRev( LocalPort, i );
				tempDist += AddedDist;

				tempAtt = NextAreas[j].curAtt;
				tempAtt += AddedDist * m_AreaPropsG[ area ].LossMult;
				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Total distance now %f\r", tempDist );
				
				// add any specific loss on the portal
				tempAtt += m_PortData[ pSndAreas->portals[i].handle - 1 ].loss;

				// check if we've visited the area.  Fast prop only visits an area once
				if( pEventAreas->bVisited )
				{
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Cancelling flood thru portal %d in previously visited area %d\r", i, area);
					continue;
				}

				if( bDistLimit && tempDist > MaxDist )
				{
					DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Distance rose above max distance at portal %d in area %d\r", i, area);
					continue;
				}

				DM_LOG(LC_SOUND, LT_DEBUG)LOGSTRING("Further expansion valid thru portal %d in area %d\r", i, area);
				
				pPortEv->Dist = tempDist;
				pPortEv->Att = tempAtt;
				pPortEv->Floods = floods;
				pPortEv->PrevPort = &pEventAreas->PortalDat[ LocalPort ];

				// add the portal destination to flooding queue
				tempQEntry.area = pSndAreas->portals[i].to;
				tempQEntry.curDist = tempDist;
				tempQEntry.curAtt = tempAtt;
				tempQEntry.portalH = pSndAreas->portals[i].handle;
				tempQEntry.PrevPort = pPortEv->PrevPort;
				tempQEntry.curLoss = 0.0f; // greebo: Initialised to 0.0f to fix gcc warning

				AddedAreas.Append( tempQEntry );
			
			} // end portal flood loop

			m_EventAreas[area].bVisited = true;

		} // end area flood loop

		// create the next expansion queue
		NextAreas = AddedAreas;

	} // end main loop

	// return true if the expansion died out naturally rather than being stopped
	return !NextAreas.Num();
}
