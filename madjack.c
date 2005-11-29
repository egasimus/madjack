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

#include <lo/lo.h>
#include <mad.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <getopt.h>
#include "config.h"


// ------- Constants -------
#define RINGBUFFER_DURATION		(5.0)
#define READ_BUFFER_SIZE		(10240)


// ------- Globals -------
jack_port_t *outport[2] = {NULL, NULL};
jack_ringbuffer_t *ringbuffer[2] = {NULL, NULL};
jack_client_t *client = NULL;

int running = 1;
int autoconnect = 0;

FILE* input_file = NULL;


#ifndef mad_f_tofloat
#define mad_f_tofloat(x)	((float)  \
				 ((x) / (float) (1L << MAD_F_FRACBITS)))
#endif




// Callback called by JACK when audio is available
static int callback_jack(jack_nframes_t nframes, void *arg)
{
    size_t to_read = sizeof (jack_default_audio_sample_t) * nframes;
	unsigned int c;
	

    // copy data to ringbuffer; one per channel
    for (c=0; c < 2; c++)
    {	
		char *buf = (char*)jack_port_get_buffer(outport[c], nframes);
		size_t len = jack_ringbuffer_read(ringbuffer[c], buf, to_read);
		
		// If we don't have enough audio, fill it up with silence
		// (this is to deal with pausing etc.)
		if (to_read > len)
			bzero( buf+len, to_read - len );
		
		//if (len < to_read)
		//	fprintf(stderr, "failed to read from ring buffer %d\n",c);
    }

	// Success
	return 0;
}



/*
 * This is the MAD input callback. The purpose of this callback is to (re)fill
 * the stream buffer which is to be decoded. In this example, an entire file
 * has been mapped into memory, so we just call mad_stream_buffer() with the
 * address and length of the mapping. When this callback is called a second
 * time, we are finished decoding.
 */

static
enum mad_flow callback_input(void *data,
		    struct mad_stream *stream)
{
	unsigned long bytes = 0;
	unsigned char *buffer = (unsigned char *)malloc( READ_BUFFER_SIZE );
	
	// No file open ?
	if (input_file==NULL)
		return MAD_FLOW_STOP;
	
	// Read in some bytes
	bytes = fread( buffer, 1, READ_BUFFER_SIZE, input_file);
	if (bytes==0)
		return MAD_FLOW_STOP;
	
	mad_stream_buffer(stream, buffer, bytes);
	
	free(buffer);
	
	return MAD_FLOW_CONTINUE;
}



/*
 * This is the output callback function. It is called after each frame of
 * MPEG audio data has been completely decoded. The purpose of this callback
 * is to output (or play) the decoded PCM audio.
 */

static
enum mad_flow callback_output(void *data,
		     struct mad_header const *header,
		     struct mad_pcm *pcm)
{
	unsigned int nchannels, nsamples;
	mad_fixed_t const *left_ch, *right_ch;
	
	/* pcm->samplerate contains the sampling frequency */
	
	nchannels = pcm->channels;
	nsamples  = pcm->length;
	left_ch   = pcm->samples[0];
	right_ch  = pcm->samples[1];
	
	
	
	// Sleep until there is room in the ring buffer
	while( jack_ringbuffer_write_space( ringbuffer[0] )
	          < (nsamples * sizeof(float) ) )
	{
		sleep(1);
		printf("Sleeping while there isn't enough room is ring buffer.\n");
	}

	
	
	
	// Put samples into the ringbuffer
	while (nsamples--) {
		jack_default_audio_sample_t sample;

		// Convert sample for left channel
		sample = mad_f_tofloat(*left_ch++);
		jack_ringbuffer_write( ringbuffer[0], (char*)&sample, sizeof(sample) );
		
		// Convert sample for right channel
		if (nchannels == 2) sample = mad_f_tofloat(*right_ch++);
		jack_ringbuffer_write( ringbuffer[1], (char*)&sample, sizeof(sample) );
	}
	
	return MAD_FLOW_CONTINUE;
}


/*
 * This is the error callback function. It is called whenever a decoding
 * error occurs. The error is indicated by stream->error; the list of
 * possible MAD_ERROR_* errors can be found in the mad.h (or stream.h)
 * header file.
 */

static
enum mad_flow callback_error(void *data,
		    struct mad_stream *stream,
		    struct mad_frame *frame)
{

  fprintf(stderr, "decoding error 0x%04x (%s)\n",
	  stream->error, mad_stream_errorstr(stream));

  /* return MAD_FLOW_BREAK here to stop decoding (and propagate an error) */

  return MAD_FLOW_CONTINUE;
}



/* Connect the chosen port to ours */
//static void connect_port(jack_client_t *client, char *port_name)
//{
//	jack_port_t *port;
//
//	// Get the port we are connecting to
//	port = jack_port_by_name(client, port_name);
//	if (port == NULL) {
///		fprintf(stderr, "Can't find port '%s'\n", port_name);
//		exit(1);
///	}

//	// Connect the port to our input port
//	fprintf(stderr,"Connecting '%s' to '%s'...\n", jack_port_name(port), jack_port_name(input_port));
//	if (jack_connect(client, jack_port_name(port), jack_port_name(input_port))) {
//		fprintf(stderr, "Cannot connect port '%s' to '%s'\n", jack_port_name(port), jack_port_name(input_port));
//		exit(1);
//	}
//}


/* Display how to use this program */
static int usage( const char * progname )
{
	fprintf(stderr, "madjack version %s\n\n", VERSION);
	fprintf(stderr, "Usage %s [<filemame>]\n\n", progname);
	exit(1);
}


void start_decoding(struct mad_decoder* decoder, char* filename )
{
	int result;

	if (input_file) {
		fclose(input_file);
		input_file = NULL;
	}
	
	input_file = fopen( filename, "r");
	
	// Start new thread for this:
	result = mad_decoder_run(decoder, MAD_DECODER_MODE_SYNC);
	fprintf(stderr, "mad_decoder_run returned %d\n", result);

}


int main(int argc, char *argv[])
{
	struct mad_decoder decoder;
	jack_status_t status;
	size_t ringbuffer_size = 0;
	int i, opt;

	while ((opt = getopt(argc, argv, "ahv")) != -1) {
		switch (opt) {
			case 'a':
				// Autoconnect our output ports
				autoconnect = 1;
				break;
		
			default:
				// Show usage/version information
				usage( argv[0] );
				break;
		}
	}



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
	fprintf(stderr,"Size of ring buffers is %2.2f seconds (%d bytes).\n", RINGBUFFER_DURATION, (int)ringbuffer_size );
	for(i=0; i<2; i++) {
		if (!(ringbuffer[i] = jack_ringbuffer_create( ringbuffer_size ))) {
			fprintf(stderr, "Cannot create ringbuffer.\n");
			exit(1);
		}
	}
	
	
	// Initalise MAD
	mad_decoder_init(
		&decoder,
		NULL, // data pointer 
		callback_input,
		NULL, // header
		NULL, // filter
		callback_output,
		callback_error,
		NULL // message
	);
	
	
	
	

	// Register the peak signal callback
	jack_set_process_callback(client, callback_jack, 0);

	// Activate ourselves
	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate JACK client.\n");
		exit(1);
	}
	
	// Create decoding thread
	start_decoding( &decoder, "test.mp3" );
	// ** DO IT **


	// Auto-connect our output ports ?
	if (autoconnect) {
		// *FIXME*
	}
	

	//while (running) {

		// Check for key press
		
		// Anything else ?
		
		sleep(1);

	//}
	
	
	
	// Shut down decoder
	mad_decoder_finish( &decoder );
	
	// Leave the jack graph 
	jack_client_close(client);
	
	// Free up the ring buffers
	jack_ringbuffer_free( ringbuffer[0] );
	jack_ringbuffer_free( ringbuffer[1] );
	

	return 0;
}

