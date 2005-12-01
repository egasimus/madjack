/*

	madjack.c
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

#include <lo/lo.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <getopt.h>


#include "control.h"
#include "mjlo.h"
#include "madjack.h"
#include "maddecode.h"
#include "config.h"



// ------- Globals -------
jack_port_t *outport[2] = {NULL, NULL};
jack_ringbuffer_t *ringbuffer[2] = {NULL, NULL};
jack_client_t *client = NULL;

input_file_t *input_file = NULL;
int state = MADJACK_STATE_EMPTY;
char * root_directory = "";





// Callback called by JACK when audio is available
static
int callback_jack(jack_nframes_t nframes, void *arg)
{
    size_t to_read = sizeof (jack_default_audio_sample_t) * nframes;
	unsigned int c;
	
	for (c=0; c < 2; c++)
	{	
		char *buf = (char*)jack_port_get_buffer(outport[c], nframes);
		size_t len = 0;

		// What state are we in ?
		if (get_state() == MADJACK_STATE_PLAYING) {
		
			// Copy data from ring buffer to output buffer
			len += jack_ringbuffer_read(ringbuffer[c], buf, to_read);
			
			// Not enough samples ?
			if (len < to_read) {
				if (is_decoding) {
					fprintf(stderr, "warning: ringbuffer underrun.\n");
				} else {
					// Must have reached end of file
				}
				
				// Stop playback
				set_state( MADJACK_STATE_STOPPED );
			}
		}
		
		// If we don't have enough audio, fill it up with silence
		// (this is to deal with pausing etc.)
		if (to_read > len)
			bzero( buf+len, to_read - len );
		
	}


	// Success
	return 0;
}





// crude way of automatically connecting up jack ports
void autoconnect_jack_ports( jack_client_t* client )
{
	const char **all_ports;
	unsigned int ch=0;
	int err,i;

	// Get a list of all the jack ports
	all_ports = jack_get_ports(client, NULL, NULL, JackPortIsInput);
	if (!all_ports) {
		fprintf(stderr, "autoconnect_jack_ports(): jack_get_ports() returned NULL.");
		exit(1);
	}
	
	// Step through each port name
	for (i = 0; all_ports[i]; ++i) {

		const char* in = jack_port_name( outport[ch] );
		const char* out = all_ports[i];
		
		fprintf(stderr, "Connecting %s to %s\n", in, out);
		
		if ((err = jack_connect(client, in, out)) != 0) {
			fprintf(stderr, "autoconnect_jack_ports(): failed to jack_connect() ports: %d\n",err);
			exit(1);
		}
	
		// Found enough ports ?
		if (++ch >= 2) break;
	}
	
	free( all_ports );
}


static
void init_jack() 
{
	jack_status_t status;
	size_t ringbuffer_size = 0;
	int i;

	// Register with Jack
	if ((client = jack_client_open("madjack", JackNullOption, &status)) == 0) {
		fprintf(stderr, "Failed to start jack client: %d\n", status);
		exit(1);
	}
	fprintf(stderr,"Registering as '%s'.\n", jack_get_client_name( client ) );


	// Create our output ports
	if (!(outport[0] = jack_port_register(client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(stderr, "Cannot register left output port.\n");
		exit(1);
	}
	
	if (!(outport[1] = jack_port_register(client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(stderr, "Cannot register left output port.\n");
		exit(1);
	}
	
	
	// Create ring buffers
	ringbuffer_size = jack_get_sample_rate( client ) * RINGBUFFER_DURATION * sizeof(float);
	fprintf(stderr,"Size of the ring buffers is %2.2f seconds (%d bytes).\n", RINGBUFFER_DURATION, (int)ringbuffer_size );
	for(i=0; i<2; i++) {
		if (!(ringbuffer[i] = jack_ringbuffer_create( ringbuffer_size ))) {
			fprintf(stderr, "Cannot create ringbuffer.\n");
			exit(1);
		}
	}


	// Register callback
	jack_set_process_callback(client, callback_jack, NULL);
	
}


static
void finish_jack()
{
	// Leave the Jack graph
	jack_client_close(client);
	
	// Free up the ring buffers
	jack_ringbuffer_free( ringbuffer[0] );
	jack_ringbuffer_free( ringbuffer[1] );
}


static
input_file_t* init_inputfile()
{
	input_file_t* ptr;
	
	// Allocate memory for data structure
	ptr = malloc( sizeof( input_file_t ) );
	if (ptr == NULL) {
		fprintf(stderr, "Failed to allocate memory for input file record.\n");
		exit(1);
	}

	// Zero memory
	bzero( ptr, sizeof( input_file_t ) );
	
	// Allocate memory for read buffer
	ptr->buffer_size = READ_BUFFER_SIZE;
	ptr->buffer = malloc( ptr->buffer_size );
	if (ptr->buffer == NULL) {
		fprintf(stderr, "Failed to allocate memory for read buffer.\n");
		exit(1);
	}
	
	return ptr;
}


static
void finish_inputfile(input_file_t* ptr)
{
	// File still open?
	if (ptr->file) {
		fclose( ptr->file );
		ptr->file = NULL;
	}

	// Free up memory used by buffer
	if (ptr->buffer) free( ptr->buffer );
	
	// Free up main data structure memory
	free( ptr );
}


const char* get_state_name( enum madjack_state state )
{
	switch( state ) {
		case MADJACK_STATE_PLAYING: return "PLAYING";
		case MADJACK_STATE_PAUSED: return "PAUSED";
		case MADJACK_STATE_READY: return "READY";
		case MADJACK_STATE_LOADING: return "LOADING";
		case MADJACK_STATE_STOPPED: return "STOPPED";
		case MADJACK_STATE_EMPTY: return "EMPTY";
		case MADJACK_STATE_QUIT: return "QUIT";
		default: return "UNKNOWN";
	}
}

enum madjack_state get_state()
{
	return state;
}


void set_state( enum madjack_state new_state )
{
	if (state != new_state) {
		printf("State changing from '%s' to '%s'.\n",
			get_state_name(state), get_state_name(new_state));
		state = new_state;
	}
}


// Display how to use this program
static
void usage()
{
	printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
	printf("Usage: %s [options] [<filename>]\n\n", PACKAGE_NAME);
	printf("  -a        Automatically connect JACK ports\n");
	printf("  -d <dir>  Set root directory for audio files\n");
	printf("  -p <port> Enable LibLO and set port\n");
	exit(1);
}



int main(int argc, char *argv[])
{
	int autoconnect = 0;
	char *port = NULL;
	int opt;


	// Parse Switches
	while ((opt = getopt(argc, argv, "ad:p:hv")) != -1) {
		switch (opt) {
			case 'a':
				autoconnect = 1;
				break;
				
			case 'd':
				root_directory = optarg;
				break;
		
			case 'p':
				port = optarg;
				break;
		
			default:
				usage();
				break;
		}
	}


	// Check remaining arguments
    argc -= optind;
    argv += optind;
    if (argc>1) {
    	printf("%s only takes a single, optional, filename argument.\n", PACKAGE_NAME);
    	usage();
	}
    


	// Initialise JACK
	init_jack();

	// Initialse Input File Data Structure
	input_file = init_inputfile();
	
	
	// Activate JACK
	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate JACK client.\n");
		exit(1);
	}
	
	// Auto-connect our output ports ?
	if (autoconnect) {
		autoconnect_jack_ports( client );
	}


	// Load an initial track ?
	if (argc) do_load( *argv );


	// Handle user keypresses (main loop)
	handle_keypresses();

	
	// Wait for decoder thread to terminate
	finish_decoder_thread();
	
	
	// Clean up JACK
	finish_jack();
	
	
	// Clean up data structure memory
	finish_inputfile( input_file );
	

	return 0;
}

