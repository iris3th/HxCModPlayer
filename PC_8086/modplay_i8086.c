///////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------//
//-------------------------------------------------------------------------------//
//-----------H----H--X----X-----CCCCC----22222----0000-----0000------11----------//
//----------H----H----X-X-----C--------------2---0----0---0----0--1--1-----------//
//---------HHHHHH-----X------C----------22222---0----0---0----0-----1------------//
//--------H----H----X--X----C----------2-------0----0---0----0-----1-------------//
//-------H----H---X-----X---CCCCC-----222222----0000-----0000----1111------------//
//-------------------------------------------------------------------------------//
//----------------------------------------------------- http://hxc2001.free.fr --//
///////////////////////////////////////////////////////////////////////////////////
// File : modpay_i8086.c
// Contains: HxCMod PC 8086 player (Real-mode) test program
//
// Written by: Jean François DEL NERO
///////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bios.h>
#include <dos.h>
#include <conio.h>
#include <i86.h>

#include "sb_io.h"

#include "../hxcmod.h"

extern unsigned char rawModData[17968];

#define DMA_PAGESIZE 8192
#define SB_SAMPLE_RATE 11025

//
// DMA Programming table
//
unsigned char DMAMask[]     = {0x0A,0x0A,0x0A,0x0A,0xD4,0xD4,0xD4,0xD4};
unsigned char DMAMode[]     = {0x0B,0x0B,0x0B,0x0B,0xD6,0xD6,0xD6,0xD6};
unsigned char DMAFlipFlop[] = {0x0C,0x0C,0x0C,0x0C,0xD8,0xD8,0xD8,0xD8};
unsigned char DMACount[]    = {0x01,0x03,0x05,0x07,0xC2,0xC6,0xCA,0xCE};
unsigned char DMAAddress[]  = {0x00,0x02,0x04,0x06,0xC0,0xC4,0xC8,0xCC};
unsigned char DMAPage[]     = {0x87,0x83,0x81,0x82,0x8F,0x8B,0x89,0x8A};

volatile unsigned char * dma_buffer;

int init_sb(int port,int irq,int dma)
{
	int cnt;
	unsigned int temp, segment, offset;
	unsigned long foo;
	unsigned char dma_page;
	unsigned int dma_offset;

	outp(0x21 + (irq & 8), inp(0x21 + (irq & 8) ) |  (0x01 << (irq&7)) ); // Mask the IRQ
	install_irq();
	outp(0x21 + (irq & 8), inp(0x21 + (irq & 8) ) & ~(0x01 << (irq&7)) ); // Enable the IRQ

	// Reset SB.
	outp(port + SB_DSP_RESET_REG,0x01);
	inp(port + SB_DSP_RESET_REG);
	inp(port + SB_DSP_RESET_REG);
	inp(port + SB_DSP_RESET_REG);
	inp(port + SB_DSP_RESET_REG);
	outp(port + SB_DSP_RESET_REG,0x00);

	cnt = 256;
	while( !(inp(port + SB_DSP_READ_BUF_IT_STATUS) & 0x80) && cnt)
	{
		cnt--;
	}

	if(cnt)
	{
		if(inp(port + SB_DSP_READ_REG) == 0xAA)
		{
			// SB reset success !

			SB_DSP_wr(port,DSP_CMD_VERSION);
			printf("SB DSP Version %d.%.2d\n",SB_DSP_rd(port),SB_DSP_rd(port));

			SB_DSP_wr(port,DSP_CMD_ENABLE_SPEAKER);  // Enable speaker

			#define SAMPLE_PERIOD (unsigned char)((65536 - (256000000/(SB_SAMPLE_RATE)))>>8)
			SB_DSP_wr(port,DSP_CMD_SAMPLE_RATE);     // Set sample rate
			SB_DSP_wr(port,SAMPLE_PERIOD);

			//////////////////////////////////////////////////////////////////////
			// Init the 8237A DMA

			outp(DMAMask[dma], (dma & 0x03) | 0x04);  // Disable channel
			outp(DMAFlipFlop[dma], 0x00);             // Clear the dma flip flop
			outp(DMAMode[dma], (dma & 0x03) | 0x58 ); // Select the transfert mode (Auto-initialized playback)
			outp(DMACount[dma], (DMA_PAGESIZE - 1) & 0xFF );
			outp(DMACount[dma],((DMA_PAGESIZE - 1)>> 8) & 0xFF );

			// Segment/Offset to DMA 20 bits Page/offset physical address
			segment = get_cur_ds();
			offset  = (unsigned int)dma_buffer;

			dma_page = ((segment & 0xF000) >> 12);
			temp = (segment & 0x0FFF) << 4;
			foo = (unsigned long)offset + (unsigned long)temp;
			if (foo > 0xFFFF)
			{
				dma_page++;
			}

			dma_offset = (unsigned int)(foo & 0xFFFF);

			printf("Buffer : 0x%.4X:0x%.4X\nDMA Page : 0x%.2x\nDMA offset : 0x%.4X\nDMA size : 0x%.4X\n", segment,offset,dma_page,dma_offset,DMA_PAGESIZE);

			if(dma_offset > (0xFFFF - DMA_PAGESIZE))
			{
				printf("Crossing dma page !!!\n");
			}

			// Set the dma page/offset.
			outp(DMAAddress[dma],  dma_offset & 0xFF );
			outp(DMAAddress[dma], (dma_offset >> 8) & 0xFF );
			outp(DMAPage[dma], dma_page );

			outp(DMAMask[dma], (dma & 0x03)); // Enable the channel

			//////////////////////////////////////////////////////////////////////

			SB_DSP_wr(port,DSP_CMD_BLOCK_TRANSFER_SIZE); // Set block transfer size
			SB_DSP_wr(port, (( (DMA_PAGESIZE / 2) - 1 ) ) & 0xFF ); // 2 ITs for the whole DMA buffer.
			SB_DSP_wr(port, (( (DMA_PAGESIZE / 2) - 1 ) ) >> 8 );

			SB_DSP_wr(port, DSP_CMD_8BITS_PCM_OUTPUT);  // Start ! (Mono 8 bits unsigned mode)

			printf("SB Init done !\n");

			return 1;
		}
	}

	return 0;
}

int main(int argc, char* argv[])
{
	int sb_port,sb_irq_int,sb_dma;
	modcontext * modctx;
	int i;

	printf("PC-8086 Real mode HxCMod Test program\n");

	sb_port = 0x220;
	sb_irq_int = 7;
	sb_dma = 1;

	it_sbport = sb_port;
	it_irq = sb_irq_int;

	printf("Init Sound Blaster : Port 0x%x, IRQ %d, DMA: %d\n",sb_port,sb_irq_int,sb_dma);

	dma_buffer = malloc(DMA_PAGESIZE*2);
	if(!dma_buffer)
	{
		printf("DMA allocation failed !\n");
		exit(-1);
	}

	for(i=0;i<DMA_PAGESIZE*2;i++)
	{
		dma_buffer[i] = 0x00;
	}

	if(!init_sb(sb_port,sb_irq_int,sb_dma))
	{
		printf("Sound Blaster init failed !\n");
		exit(-1);
	}

	modctx = malloc(sizeof(modcontext));
	if(modctx)
	{
		if( hxcmod_init( modctx ) )
		{
			printf("HxCMOD init done !\n");

			hxcmod_setcfg( modctx, SB_SAMPLE_RATE, 0, 0);

			printf("Sound configuration done !\n");

			if(hxcmod_load( modctx, rawModData, sizeof(rawModData) ))
			{
				while(1)
				{
					if(it_flag)
					{
						it_flag = 0x00;

						printf("it toggle %d\n",it_toggle);

						if(it_toggle)
						{
							hxcmod_fillbuffer( modctx, (msample *)&dma_buffer[0], DMA_PAGESIZE/2, NULL );
						}
						else
						{
							hxcmod_fillbuffer( modctx, (msample *)&dma_buffer[DMA_PAGESIZE/2], DMA_PAGESIZE/2, NULL );
						}
					}
				}
			}

			hxcmod_unload( modctx );
		}

		free(modctx);
	}
	else
	{
		printf("Malloc failed !\n");
	}

	return 0;
}