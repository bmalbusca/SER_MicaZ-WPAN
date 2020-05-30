/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Demonstrates how to use broadcast and unicast
 * \author
 *         Adam Dunkels <adam@sics.se>
 *
 *         This example shows how to send broadcast and unicast, as
 *         well as how to use the Contiki memory block manager (MEMB)
 *         and the Contiki list library (LIST) to keep track of
 *         neighbors. The program consists of two processes, one that
 *         periodically sends broadcast messages and one that
 *         periodically sends unicast messages to random neighbors. A
 *         list of neighbors is maintained. The list is populated from
 *         the reception of broadcast messages from neighbors. The
 *         neighbor list keeps a simple set of quality metrics for
 *         each neighbor: a moving average of sequence number gaps,
 *         which indicates the number of broadcast packets that have
 *         been lost; a the last RSSI received; and the last LQI
 *         received.
 */


#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime/rime.h"

#include <stdio.h>

/* This is the structure of broadcast messages. */
struct broadcast_message {
  uint8_t seqno;
  uint8_t id;
};

/* This is the structure of unicast ping messages. */
struct unicast_message {
  //se for ack =1 , c.c. 0
  uint8_t ack;
  uint8_t type;
  uint8_t temp;

};

/* These are the types of unicast messages that we can send. */
enum {
  UNICAST_TYPE_PING,
  UNICAST_TYPE_PONG
};

/* This structure holds information about neighbors. */
struct neighbor {
  /* The ->next pointer is needed since we are placing these on a
     Contiki list. */
  struct neighbor *next;

  /* The ->addr field holds the Rime address of the neighbor. */
  linkaddr_t addr;
  struct ctimer ctimer;
  /* Each broadcast packet contains a sequence number (seqno). The
     ->last_seqno field holds the last sequenuce number we saw from
     this neighbor. */
  uint8_t last_seqno;

};

/* This #define defines the maximum amount of neighbors we can remember. */
#define MAX_NEIGHBORS 16
#define NEIGHBOR_TIMEOUT 60 * CLOCK_SECOND

/* This MEMB() definition defines a memory pool from which we allocate
   neighbor entries. */
MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);

/* The neighbors_list is a Contiki list that holds the neighbors we
   have seen thus far. */
LIST(neighbors_list);

/* These hold the broadcast and unicast structures, respectively. */
static struct broadcast_conn broadcast;
static struct unicast_conn unicast;

/* These two defines are used for computing the moving average for the
   broadcast sequence number gaps. */
#define SEQNO_EWMA_UNITY 0x100
#define SEQNO_EWMA_ALPHA 0x040

/*---------------------------------------------------------------------------*/
/*
 * This function is called by the ctimer present in each neighbor
 * table entry. The function removes the neighbor from the table
 * because it has become too old.
 */
static void
remove_neighbor(void *n)
{
  struct neighbor *e = n;
  printf("removed node %d from the list\n", e->addr);
  list_remove(neighbors_list, e);
  memb_free(&neighbors_memb, e);
}

/*---------------------------------------------------------------------------*/
/* We first declare our two processes. */
PROCESS(broadcast_process, "Broadcast process");
PROCESS(unicast_process, "Unicast process");

/* The AUTOSTART_PROCESSES() definition specifices what processes to
   start when this module is loaded. We put both our processes
   there. */
AUTOSTART_PROCESSES(&broadcast_process, &unicast_process);
/*---------------------------------------------------------------------------*/
/* This function is called whenever a broadcast message is received. */
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  struct neighbor *n;
  struct broadcast_message *m;
  //uint8_t seqno_gap;

  /* The packetbuf_dataptr() returns a pointer to the first data byte
     in the received packet. */
  m = packetbuf_dataptr();

  /* Check if we already know this neighbor. */
  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {

    /* We break out of the loop if the address of the neighbor matches
       the address of the neighbor from which we received this
       broadcast message. */
    if(linkaddr_cmp(&n->addr, from)) {
      /* Our neighbor was found, so we update the timeout. */
      ctimer_set(&n->ctimer, NEIGHBOR_TIMEOUT, remove_neighbor, n);
      break;
    }
  }

  /* If n is NULL, this neighbor was not found in our list, and we
     allocate a new struct neighbor from the neighbors_memb memory
     pool. */
  if(n == NULL) {
    if(m->id == 0){
      return;
    }else{
      n = memb_alloc(&neighbors_memb);

      /* If we could not allocate a new neighbor entry, we give up. We
         could have reused an old neighbor entry, but we do not do this
         for now. */
      if(n == NULL) {
        return;
      }

      /* Initialize the fields. */
      linkaddr_copy(&n->addr, from);
      n->last_seqno = m->seqno - 1;
   
      
      ctimer_set(&n->ctimer, NEIGHBOR_TIMEOUT, remove_neighbor, n);
      /* Place the neighbor on the neighbor list. */
      list_add(neighbors_list, n);
    }
  }

  /* Remember last seqno we heard. */
  n->last_seqno = m->seqno;

  /* Print out a message. */
  printf("broadcast message received from %d.%d with seqno %d, id %d \n",
         from->u8[0], from->u8[1],
         m->seqno,
         m->id);
}
/* This is where we define what function to be called when a broadcast
   is received. We pass a pointer to this structure in the
   broadcast_open() call below. */
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
/*---------------------------------------------------------------------------*/

/* This function is called for every incoming unicast packet. */
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  struct unicast_message *msg;

  /* Grab the pointer to the incoming data. */
  msg = packetbuf_dataptr();

  /* We have two message types, UNICAST_TYPE_PING and
     UNICAST_TYPE_PONG. If we receive a UNICAST_TYPE_PING message, we
     print out a message and return a UNICAST_TYPE_PONG. */
  if(msg->type == UNICAST_TYPE_PING) {
    printf("unicast ping received from %d.%d\n",
           from->u8[0], from->u8[1]);
    msg->type = UNICAST_TYPE_PONG;
    packetbuf_copyfrom(msg, sizeof(struct unicast_message));
    /* Send it back to where it came from. */
    unicast_send(c, from);
  }
  if(msg->type == UNICAST_TYPE_PONG) {
    printf("unicast ACK received from %d.%d\n",
           from->u8[0], from->u8[1]);
  }
}

static const struct unicast_callbacks unicast_callbacks = {recv_uc};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(broadcast_process, ev, data)
{
  static struct etimer et;
  static uint8_t seqno;
  struct broadcast_message msg;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Send a broadcast every 16 - 32 seconds */
    etimer_set(&et, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    //id receiver = 1 , id sender/sensor = 0
    msg.id = 0;
    msg.seqno = seqno;
    packetbuf_copyfrom(&msg, sizeof(struct broadcast_message));
    broadcast_send(&broadcast);
    seqno++;
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_process, ev, data)
{
  PROCESS_EXITHANDLER(unicast_close(&unicast);)
    
  PROCESS_BEGIN();

  unicast_open(&unicast, 146, &unicast_callbacks);

  while(1) {
    static struct etimer et;
    struct unicast_message msg;
    struct neighbor *n;
    int randneighbor, i;
    
    etimer_set(&et, CLOCK_SECOND * 8 + random_rand() % (CLOCK_SECOND * 8));
    
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    /* Pick a random neighbor from our list and send a unicast message to it. */
    if(list_length(neighbors_list) > 0) {
      randneighbor = random_rand() % list_length(neighbors_list);
      n = list_head(neighbors_list);
      for(i = 0; i < randneighbor; i++) {
        n = list_item_next(n);
      }
      printf("sending unicast to %d.%d\n", n->addr.u8[0], n->addr.u8[1]);

      
      msg.type = UNICAST_TYPE_PING;
      packetbuf_copyfrom(&msg, sizeof(msg));
      unicast_send(&unicast, &n->addr);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
