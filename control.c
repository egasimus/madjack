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
	
	// Make sure stdin is a terminal
	if (!isatty (STDIN_FILENO)) return;

	tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}


static void
set_input_mode (void)
{
	struct termios tattr;
	
	// Make sure stdin is a terminal
	if (!isatty(STDIN_FILENO)) return;
	
	// Save the terminal attributes so we can restore them later
	if (tcgetattr(STDIN_FILENO, &saved_attributes) == -1) {
		perror("failed to get termios settings");
		exit(-1);
	}
	atexit (reset_input_mode);
	
	// Set the funky terminal modes.
	tcgetattr (STDIN_FILENO, &tattr);
	tattr.c_lflag &= ~(ICANON|ECHO); // Clear ICANON and ECHO.
	tattr.c_cc[VMIN] = 1;
	tattr.c_cc[VTIME] = 0;
	tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}

// Read in the name of a file from STDIN
static
char* read_filename()
{
	char *filename = malloc(MAX_FILENAME_LEN);
	int i;
	
	reset_input_mode();

	printf("Enter name of file to load: ");
	fgets( filename, MAX_FILENAME_LEN-1, stdin );
	
	// Remove carrage return from end of filename
	for(i=0; i<MAX_FILENAME_LEN; i++) {
		if (filename[i]==10 || filename[i]==13) filename[i]=0;
	}

	set_input_mode( );
	
	return filename;
}

// Concatinate a filename on the end of the root path
static
char* build_fullpath( const char* root, const char* name )
{
	int len = 0;
	char* filepath;
	
	// Calculate length of full filepath
	len = strlen(root_directory) + strlen(name) + 2;
	filepath = malloc( len );
	if (!filepath) {
		perror("failed to allocate memory for filepath");
		exit(1);
	}
	
	// Copy root directory to start of filepath
	strcpy( filepath, root_directory );
	
	// Remove surplus trailing slash(es)
	len = strlen(filepath);
	while( len > 1 && filepath[len-1] == '/' ) {
		filepath[--len] = '\0';
	}
	
	// Append a slash
	if (len) filepath[len++] = '/';
	
	// Append the filename
	strcpy( filepath+len, name );
	
	return filepath;
}


static
void extract_filename( input_file_t *input )
{
	int i;
	
	// Check pointer is valid
	if (input==NULL) return;
	
	// Find the last slash by working backwards from the end
	for(i=strlen(input->filepath); i>=1 && input->filepath[i-1]!='/'; i-- );
	
	// Copy it across
	strncpy( input->filename, input->filepath+i, MAX_FILENAME_LEN );
	
	// Remove the suffix
	for(i=strlen(input->filename); i>=1; i-- ) {
		if (input->filename[i] == '.') {
			input->filename[i] = '\0';
			break;
		}
	}
	
	if (verbose) printf("filename: %s\n", input->filename);

}




// Start playing
void do_play()
{
	if (verbose) printf("-> do_play()\n");
	
	if (get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_READY)
	{
		set_state( MADJACK_STATE_PLAYING );
	}
	else if (get_state() == MADJACK_STATE_STOPPED)
	{
		// Re-cue up the the track
		do_cue();
		
		// Wait until track is loaded
		while ( get_state() == MADJACK_STATE_LOADING ) {
			if (verbose) printf("Waiting for decoder thread to start.\n");
			usleep( 5000 );
		}
		
		set_state( MADJACK_STATE_PLAYING );
	}
	else if (get_state() != MADJACK_STATE_PLAYING)
	{
		fprintf(stderr, "Warning: Can't change from %s to state PLAYING.\n", get_state_name(get_state()) );
	}
}


// Prepare deck to go into 'READY' state
void do_cue()
{
	if (verbose) printf("-> do_cue()\n");
	
	// Stop first
	if (get_state() == MADJACK_STATE_PLAYING ||
	    get_state() == MADJACK_STATE_PAUSED)
	{
		set_state( MADJACK_STATE_STOPPED );
	}
	
	// Start the new thread
	if (get_state() == MADJACK_STATE_LOADING ||
	    get_state() == MADJACK_STATE_STOPPED )
	{

		// Ensure the old thread is stopped
		while( is_decoding ) {
			if (verbose) printf("Waiting for decoder thread to stop.\n");
			usleep( 5000 );
		}
	
		// Set the decoder running
		set_state( MADJACK_STATE_LOADING );
		start_decoder_thread( input_file );
		
	}
	else if (get_state() != MADJACK_STATE_READY)
	{
		fprintf(stderr, "Warning: Can't change from %s to state READY.\n", get_state_name(get_state()) );
	}
}


// Pause Deck (if playing)
void do_pause()
{
	if (verbose) printf("-> do_pause()\n");
	
	if (get_state() == MADJACK_STATE_PLAYING )
	{
		set_state( MADJACK_STATE_PAUSED );
	}
	else if (get_state() != MADJACK_STATE_PAUSED)
	{
		fprintf(stderr, "Warning: Can't change from %s to state PAUSED.\n", get_state_name(get_state()) );
	}
}


// Stop deck (and close down decoder)
void do_stop()
{
	if (verbose) printf("-> do_stop()\n");

	if (get_state() == MADJACK_STATE_PLAYING ||
	    get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_READY )
	{
		set_state( MADJACK_STATE_STOPPED );
	}
	else if (get_state() != MADJACK_STATE_STOPPED)
	{
		fprintf(stderr, "Warning: Can't change from %s to state STOPPED.\n", get_state_name(get_state()) );
	}

}


// Eject track from Deck
void do_eject()
{
	if (verbose) printf("-> do_eject()\n");

	// Stop first
	if (get_state() == MADJACK_STATE_PLAYING ||
	    get_state() == MADJACK_STATE_PAUSED ||
	    get_state() == MADJACK_STATE_READY)
	{
		set_state( MADJACK_STATE_STOPPED );
	}
	
	if (get_state() == MADJACK_STATE_STOPPED ||
	    get_state() == MADJACK_STATE_ERROR)
	{

		// Wait for decoder to finish
		while( is_decoding ) {
			if (verbose) printf("Waiting for decoder thread to stop.\n");
			usleep( 5000 );
		}

		// Close the input file
		if (input_file->file) {
			fclose(input_file->file);
			input_file->file = NULL;
			free(input_file->filepath);
			input_file->filepath = NULL;
			input_file->filename[0] = '\0';
		}
		
		// Reset position
		position = 0.0;

		// Deck is now empty
		set_state( MADJACK_STATE_EMPTY );
	}
	else if (get_state() != MADJACK_STATE_EMPTY)
	{
		fprintf(stderr, "Warning: Can't change from %s to state EMPTY.\n", get_state_name(get_state()) );
	}
	
}



// Load Track into Deck
void do_load( const char* filepath )
{
	if (verbose) printf("-> do_load(%s)\n", filepath);
	
	// Can only load if Deck is empty
	if (get_state() != MADJACK_STATE_EMPTY )
	{
		do_eject();
	}
	
	
	// Check it really is empty
	if (get_state() == MADJACK_STATE_EMPTY )
	{
		char* fullpath;
	
		set_state( MADJACK_STATE_LOADING );
		
		// Pre-pend the root directory path
		fullpath = build_fullpath( root_directory, filepath );
		if (!quiet) printf("Loading: %s\n", fullpath);
		
		// Open the new file
		input_file->file = fopen( fullpath, "r" );
		free( fullpath );
		if (input_file->file==NULL) {
			perror("Failed to open input file");
			set_state( MADJACK_STATE_ERROR );
			return;
		}
		
		// Copy string
		input_file->filepath = strdup( filepath );
		
		// Extract the filename (minus extension) from the path
		extract_filename( input_file );
		
		// Cue up the new file	
		do_cue();
	}
	else if (get_state() != MADJACK_STATE_EMPTY)
	{
		fprintf(stderr, "Warning: Can't change from %s to state LOADING.\n", get_state_name(get_state()) );
	}
	
}

// Quit MadJack
void do_quit()
{
	if (verbose) printf("-> do_quit()\n");
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
	printf( "  c: Cue deck to start of track\n" );
	printf( "  q: Quit MadJack\n" );
	printf( "\n" );	
}



void handle_keypresses()
{
	struct timeval timeout;
	fd_set readfds;
	int retval = -1;

	// Make STDOUT unbuffered (if it is a terminal)
	if (isatty(STDOUT_FILENO))
		setbuf(stdout, NULL);

	// Turn off input buffering on STDIN
	set_input_mode( );
	
	// Check for keypresses
	while (get_state() != MADJACK_STATE_QUIT) {

		// Display position
		if (!quiet && isatty(STDOUT_FILENO))
			printf("[%1.1f]    \r", position);

		// Set timeout to 1/10 second
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;

		// Watch socket to see when it has input.
		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		retval = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);

		// Check return value 
		if (retval < 0) {
			// Something went wrong
			if (verbose) perror("select()");
			return;
			
		} else if (retval > 0) {

			// Get keypress
			int c = tolower( fgetc( stdin ) );
			switch(c) {
			
				// Pause/Play
				case 'p': 
					if (get_state() == MADJACK_STATE_PLAYING) {
						do_pause();
					} else {
						do_play();
					}
				break;
				
				// Load
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
				
				// Ignore return and enter
				case 13:
				case 10:
				break;
			}
		}
	}

	
}

