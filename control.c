/*

	control.c
	MPEG Audio Deck for the jack audio connection kit
	Copyright (C) 2005  Nicholas J. Humfrey
	
	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>

#include "control.h"
#include "madjack.h"
#include "maddecode.h"
#include "config.h"



// ------- Globals -------
struct termios saved_attributes;



static void
reset_input_mode (void)
{
	tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}


static void
set_input_mode (void)
{
	struct termios tattr;
	
	// Make sure stdin is a terminal.
	if (!isatty (STDIN_FILENO))
	{
		fprintf (stderr, "Not a terminal.\n");
		exit (-1);
	}
	
	// Save the terminal attributes so we can restore them later.
	if (tcgetattr(STDIN_FILENO, &saved_attributes) == -1) {
		perror("failed to get termios settings");
		exit(-1);
	}
	atexit (reset_input_mode);
	
	// Set the funny terminal modes.
	tcgetattr (STDIN_FILENO, &tattr);
	tattr.c_lflag &= ~(ICANON|ECHO); // Clear ICANON and ECHO.
	tattr.c_cc[VMIN] = 1;
	tattr.c_cc[VTIME] = 0;
	tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}






// Start playing
void do_play()
{
	printf("do_play()\n");
	
	if (get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_READY)
	{
		set_state( MADJACK_STATE_PLAYING );
		
	} else if (get_state() == MADJACK_STATE_STOPPED) {
	
		// Re-cue up the the track
		do_cue();
		
		// Wait until track is loaded
		while ( get_state() == MADJACK_STATE_LOADING ) {
			printf("Waiting for decoder thread to load.\n");
			usleep( 5000 );
		}
		
		set_state( MADJACK_STATE_PLAYING );
		
	} else {
		printf( "Can't change to PLAYING from state %d.\n", get_state() );
	}
}


// Prepare deck to go into 'READY' state
void do_cue()
{
	printf("do_cue()\n");
	
	if (get_state() == MADJACK_STATE_PLAYING ||
	    get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_STOPPED )
	{

		// Change to stopped	
		set_state( MADJACK_STATE_STOPPED );


		// Wait for the old thread to stop
		while( is_decoding ) {
			printf("Waiting for old thread to stop.\n");
			usleep( 5000 );
		}
	
		// Seek to start of file
		// FIXME: seek to after ID3 tags
		fseek( input_file->file, 0, SEEK_SET);
		
		// Set the decoder running
		set_state( MADJACK_STATE_LOADING );
		start_decoder_thread( input_file );
		
	}
	else if (get_state() != MADJACK_STATE_READY)
	{
	
		printf( "Can't change to READY from state %d.\n", get_state() );
		
	}
	
}


// Pause Deck (if playing)
void do_pause()
{
	printf("do_pause()\n");
	
	if (get_state() == MADJACK_STATE_PLAYING )
	{
		set_state( MADJACK_STATE_PAUSED );
	}
	else if (get_state() != MADJACK_STATE_PAUSED)
	{
		printf( "Can't change to PAUSED from state %d.\n", get_state() );
	}
}


// Stop deck (and close down decoder)
void do_stop()
{
	printf("do_stop()\n");
	set_state( MADJACK_STATE_STOPPED );

	if (get_state() == MADJACK_STATE_PLAYING ||
	    get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_READY )
	{
		set_state( MADJACK_STATE_STOPPED );
	}
	else if (get_state() != MADJACK_STATE_STOPPED)
	{
		printf( "Can't change to STOPPED from state %d.\n", get_state() );
	}

}


// Eject track from Deck
void do_eject()
{
	printf("do_eject()\n");

	if (get_state() == MADJACK_STATE_PLAYING ||
	    get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_READY ||
	    get_state() == MADJACK_STATE_STOPPED )
	{
		// Stop first
		set_state( MADJACK_STATE_STOPPED );
	
		// Wait for decoder to finish
		while( is_decoding ) {
			printf("Waiting for decoder to stop.\n");
			usleep( 5000 );
		}

		// Close the input file
		if (input_file->file) {
			fclose(input_file->file);
			input_file->file = NULL;
			input_file->filepath = NULL;
		}

		// Deck is now empty
		set_state( MADJACK_STATE_EMPTY );
	}
	else if (get_state() != MADJACK_STATE_EMPTY)
	{
		printf( "Can't change to EMPTY from state %d.\n", get_state() );
	}
	
}


// Load Track into Deck
void do_load( char* name )
{
	printf("do_load(%s)\n", name);
	
	// Can only load if Deck is empty
	if (get_state() != MADJACK_STATE_EMPTY )
	{
		do_eject();
	}
	
	
	// Check it really is empty
	if (get_state() == MADJACK_STATE_EMPTY )
	{
		// Open the new file
		input_file->filepath = name;
		input_file->file = fopen( input_file->filepath, "r" );
		if (input_file->file==NULL) {
			perror("Failed to open input file");
			return;
		}
		
		// We are now effectively in the 'STOPPED' state
		set_state( MADJACK_STATE_STOPPED );
		
		// Cue up the new file	
		do_cue();
	}
}

// Quit MadJack
void do_quit()
{
	printf("do_quit()\n");
	set_state( MADJACK_STATE_QUIT );
}



void display_keyhelp()
{
	printf( "%s version %s\n", PACKAGE_NAME, PACKAGE_VERSION );
	printf( "  h: This Screen\n" );
	printf( "  p: Play/Pause Deck\n" );
	printf( "  l: Load a Track\n" );
	printf( "  e: Eject current track\n" );
	printf( "  s: Stop Deck\n" );
	printf( "  c: Cue Deck\n" );
	printf( "  q: Quit MadJack\n" );
	printf( "\n" );	
}


char* read_filename()
{
	char *filename = malloc(255);
	int i;
	
	reset_input_mode();

	printf("Enter name of file to load: ");
	fgets( filename, 254, stdin );
	for(i=0; i<254; i++) {
		if (filename[i]==10 || filename[i]==13) {
			filename[i]=0;
		}
	}

	set_input_mode( );
	
	return filename;
}


void handle_keypresses()
{
	// Turn off input buffering on STDIN
	set_input_mode( );
	
	// Check for keypresses
	while (get_state() != MADJACK_STATE_QUIT) {

		// Get keypress
		int c = tolower( fgetc( stdin ) );
		switch(c) {
			case 'p': 
				if (get_state() == MADJACK_STATE_PLAYING) {
					do_pause();
				} else {
					do_play();
				}
			break;
			
			case 'l': {
				char* filename = read_filename();
				do_load( filename );
				free( filename );
				break;
			}

			case 'c': do_cue(); break;
			case 'e': do_eject(); break;
			case 's': do_stop(); break;
			case 'q': do_quit(); break;
			
			
			default:
				printf( "Unknown command '%c'.\n", (char)c );
			case 'h':
			case '?':
				display_keyhelp();
			break;
			
			// Ignore return
			case 13:
			case 10:
			break;
		}
	}

	
}

