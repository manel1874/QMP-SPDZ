#include "OT/BaseOT.h"
#include "Tools/random.h"
#include "Tools/benchmarking.h"

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <pthread.h>

extern "C" {
//#include "SimpleOT/ot_sender.h"
//#include "SimpleOT/ot_receiver.h"
//#include "quantum_random_oblivious_transfer/qot_receiver.h"
//#include "quantum_random_oblivious_transfer/qot_sender.h"
#include "OTKeys/include/ui_rotk/receiver_uirotk.h"
#include "OTKeys/include/ui_rotk/sender_uirotk.h"
}

using namespace std;

const char* role_to_str(OT_ROLE role)
{
    if (role == RECEIVER)
        return "RECEIVER";
    if (role == SENDER)
        return "SENDER";
    return "BOTH";
}

OT_ROLE INV_ROLE(OT_ROLE role)
{
    if (role == RECEIVER)
        return SENDER;
    if (role == SENDER)
        return RECEIVER;
    else
        return BOTH;
}

void send_if_ot_sender(TwoPartyPlayer* P, vector<octetStream>& os, OT_ROLE role)
{
    if (role == SENDER)
    {
        P->send(os[0]);
    }
    else if (role == RECEIVER)
    {
        P->receive(os[1]);
    }
    else
    {
        // both sender + receiver
        P->send_receive_player(os);
    }
}

void send_if_ot_receiver(TwoPartyPlayer* P, vector<octetStream>& os, OT_ROLE role)
{
    if (role == RECEIVER)
    {
        P->send(os[0]);
    }
    else if (role == SENDER)
    {
        P->receive(os[1]);
    }
    else
    {
        // both
        P->send_receive_player(os);
    }
}


void BaseOT::exec_base(int my_num, int other_player, bool new_receiver_inputs)
{
    if (not cpu_has_avx())
        throw runtime_error("SimpleOT needs AVX support");


    printf("Just checking my_num: %d\n", my_num);
    printf("Just checking other_player: %d\n", other_player);

    int i, j; //k;
    size_t len;
    PRNG G;
    G.ReSeed();
    vector<octetStream> os(2), os1(2), os2(2); //object for sending information between the players
    //SIMPLEOT_SENDER sender;
    //SIMPLEOT_RECEIVER receiver;

    OKDOT_SENDER qsender; //sender structure
    qsender.my_num = my_num;
    qsender.other_player = other_player;
    
    OKDOT_RECEIVER qreceiver; //receiver structure
    qreceiver.my_num = my_num;
    qreceiver.other_player = other_player;

    unsigned char sender_out[2][OUTPUT_LENGTH/32]; //stores sender outputs
    unsigned char receiver_out[OUTPUT_LENGTH/32]; //stores receiver outputs

    unsigned int sender_indexlist[2][KEY_LENGTH/2]; //stores the indexlist received by the sender in int format
    unsigned char receiver_char_indexlist[2][KEY_LENGTH]; //stores the indexlist generated by the receiver in char format
    unsigned char sender_char_indexlist[2][KEY_LENGTH]; //stores the indexlist received by the sender in char format

    unsigned char v_char[2][12*8]; //stores the random numbers for calculating the hash in char format - generated by the sender
    unsigned long long int v_sender[2][12]; //stores the random numbers for calculating the hash in long long int format - generated by the sender
    unsigned char u_char[2][12*8]; //stores the random numbers for calculating the hash that the receiver got from the sender
    unsigned long long int u_receiver[2][12]; //stores in long long int format the random numbers that the receiver got from the sender 
    



    for (i=0; i<nOT; i++)
    {
	    /* generate sender oblivious keys */
	    if (ot_role & SENDER)
	    {
		    sender_okd(&qsender);
	    }


	    /* generate receiver oblivious keys and indexlists */
	    if (ot_role & RECEIVER)
	    {
		    receiver_okd(&qreceiver);
		    receiver_indexlist(&qreceiver);
		    for (j=0; j<KEY_LENGTH/2; j++) //convert the indexlist from ints to chars (since the ints are large, we will need 2 chars to store 1 int)
		    {
			    receiver_char_indexlist[0][2*j] = (qreceiver.indexlist[0][j]>>8) & 0xFF;
			    receiver_char_indexlist[0][2*j+1] = qreceiver.indexlist[0][j] & 0xFF; 
			    receiver_char_indexlist[1][2*j] = (qreceiver.indexlist[1][j]>>8) & 0xFF;
			    receiver_char_indexlist[1][2*j+1] = qreceiver.indexlist[1][j] & 0xFF; 	
		    }


		    if (new_receiver_inputs) //generates a random receiver input if new_receiver_inputs = true
		    	receiver_inputs[i] = G.get_uchar()&1;
		    //printf ("\nReceiver's input: %d\n", receiver_inputs[i].get());
		    

		    if (receiver_inputs[i].get() == 0) //the receiver sends the indexlists to the sender in a different order according to the receiver input
		    {
			    os1[0].store_bytes(receiver_char_indexlist[0], sizeof(receiver_char_indexlist[0]));
			    os2[0].store_bytes(receiver_char_indexlist[1], sizeof(receiver_char_indexlist[1]));
		    }
		    else if (receiver_inputs[i].get() == 1)
		    {
			    os1[0].store_bytes(receiver_char_indexlist[1], sizeof(receiver_char_indexlist[1]));
			    os2[0].store_bytes(receiver_char_indexlist[0], sizeof(receiver_char_indexlist[0]));
		    }
	    }
	    send_if_ot_receiver(P, os1, ot_role); //sending the indexlists to the sender
	    send_if_ot_receiver(P, os2, ot_role);


	    /* receive the indexlists generated by the receiver */
	    if (ot_role & SENDER)
	    {
		    os1[1].get_bytes((octet*)sender_char_indexlist[0], len); //the sender receives the indexlists and stores them in his own array
		    os2[1].get_bytes((octet*)sender_char_indexlist[1], len);

		    for (j=0; j<KEY_LENGTH/2; j++) //converts the indexlists into int format
		    {
			    sender_indexlist[0][j] = (sender_char_indexlist[0][2*j]<<8) | (sender_char_indexlist[0][2*j+1]);
			    sender_indexlist[1][j] = (sender_char_indexlist[1][2*j]<<8) | (sender_char_indexlist[1][2*j+1]);
		    }
	    }

	    os1[0].reset_write_head();
	    os2[0].reset_write_head();


	    /* calculate hash functions and generate sender output */
	    if (ot_role & SENDER)
	    {
		    for (j=0; j<12*8; j++) //generate random chars
		    {
			    v_char[0][j] = G.get_uchar();
			    v_char[1][j] = G.get_uchar();
		    }

		    os1[0].store_bytes(v_char[0], sizeof(v_char[0])); //send the randomly generated chars to the receiver
		    os2[0].store_bytes(v_char[1], sizeof(v_char[1]));

		    for (j=0; j<12; j++) //store the generated chars in long long int format
		    {
			    v_sender[0][j] = ((unsigned long long int)(v_char[0][8*j])<<56) | ((unsigned long long int)(v_char[0][8*j+1])<<48) 
				    | ((unsigned long long int)(v_char[0][8*j+2])<<40) | ((unsigned long long int)(v_char[0][8*j+3])<<32) 
				    | ((unsigned long long int)(v_char[0][8*j+4])<<24) | ((unsigned long long int)v_char[0][8*j+5]<<16) 
				    | ((unsigned long long int)v_char[0][8*j+6]<<8) | (unsigned long long int)v_char[0][8*j+7];
			    v_sender[1][j] = ((unsigned long long int)v_char[1][8*j]<<56) | ((unsigned long long int)v_char[1][8*j+1]<<48) 
				    | ((unsigned long long int)v_char[1][8*j+2]<<40) | ((unsigned long long int)v_char[1][8*j+3]<<32) 
				    | ((unsigned long long int)v_char[1][8*j+4]<<24) | ((unsigned long long int)v_char[1][8*j+5]<<16) 
				    | ((unsigned long long int)v_char[1][8*j+6]<<8) | (unsigned long long int)v_char[1][8*j+7];
		    }

		    sender_output(&qsender, v_sender[0], v_sender[1], sender_indexlist[0], sender_indexlist[1], sender_out); //generate sender output
		    //for (j=0; j<16; j++)
			    //printf ("Sender's output 0: %x   Sender's output 1: %x  \n", sender_out[0][j], sender_out[1][j]);
		    //printf ("\n\n");
			
		    
		    for (j=0; j<OUTPUT_LENGTH/32; j++) //store the sender output in the BaseOT object
		    {
			    sender_inputs[i][0].set_byte(3+4*j, sender_out[0][j] & 0xFF);
			    sender_inputs[i][0].set_byte(2+4*j, (sender_out[0][j]>>8) & 0xFF);
			    sender_inputs[i][0].set_byte(1+4*j, (sender_out[0][j]>>16) & 0xFF);
			    sender_inputs[i][0].set_byte(0+4*j, (sender_out[0][j]>>24) & 0xFF);

			    sender_inputs[i][1].set_byte(3+4*j, (sender_out[1][j]) & 0xFF);
			    sender_inputs[i][1].set_byte(2+4*j, (sender_out[1][j]>>8) & 0xFF);
			    sender_inputs[i][1].set_byte(1+4*j, (sender_out[1][j]>>16) & 0xFF);
			    sender_inputs[i][1].set_byte(0+4*j, (sender_out[1][j]>>24) & 0xFF);
		    }

	    }
	    send_if_ot_sender(P, os1, ot_role); //send randomly generated chars to the receiver
	    send_if_ot_sender(P, os2, ot_role);



	    if (ot_role & RECEIVER)
	    {
		    os1[1].get_bytes((octet*)u_char[0], len); //receive randomly generated chars from the sender
		    os2[1].get_bytes((octet*)u_char[1], len);

		    for (j=0; j<12; j++) //store the random chars in long long int format
		    {
			    u_receiver[0][j] = ((unsigned long long int)u_char[0][8*j]<<56) | ((unsigned long long int)u_char[0][8*j+1]<<48) 
				    | ((unsigned long long int)u_char[0][8*j+2]<<40) | ((unsigned long long int)u_char[0][8*j+3]<<32) 
				    | ((unsigned long long int)u_char[0][8*j+4]<<24) | ((unsigned long long int)u_char[0][8*j+5]<<16) 
				    | ((unsigned long long int)u_char[0][8*j+6]<<8) | (unsigned long long int)u_char[0][8*j+7];
			    u_receiver[1][j] = ((unsigned long long int)u_char[1][8*j]<<56) | ((unsigned long long int)u_char[1][8*j+1]<<48) 
				    | ((unsigned long long int)u_char[1][8*j+2]<<40) | ((unsigned long long int)u_char[1][8*j+3]<<32) 
				    | ((unsigned long long int)u_char[1][8*j+4]<<24) | ((unsigned long long int)u_char[1][8*j+5]<<16) 
				    | ((unsigned long long int)u_char[1][8*j+6]<<8) | (unsigned long long int)u_char[1][8*j+7];
		    }

		    receiver_output(&qreceiver, u_receiver[receiver_inputs[i].get()], receiver_out); //generate receiver output
		    //for (j=0; j<16; j++)
			    //printf ("Receiver's output: %x  \n",receiver_out[j]);
		    //printf ("\n\n");
			
		    
		    for (j=0; j<OUTPUT_LENGTH/32; j++) //store the receiver output in the BaseOT object
		    {
			    receiver_outputs[i].set_byte(3+4*j, (receiver_out[j]) & 0xFF);
			    receiver_outputs[i].set_byte(2+4*j, (receiver_out[j]>>8) & 0xFF);
			    receiver_outputs[i].set_byte(1+4*j, (receiver_out[j]>>16) & 0xFF);
			    receiver_outputs[i].set_byte(0+4*j, (receiver_out[j]>>24) & 0xFF);
		    }

	    }
	    os1[0].reset_write_head();
	    os2[0].reset_write_head();

    }

/************************************************************************************************************************/

/*

    if (ot_role & SENDER)
    {
	
        sender_genS(&sender, S_pack);
        os[0].store_bytes(S_pack, sizeof(S_pack));
    }
    send_if_ot_sender(P, os, ot_role);

    if (ot_role & RECEIVER)
    {
        os[1].get_bytes((octet*) receiver.S_pack, len);
        if (len != HASHBYTES)
        {
            cerr << "Received invalid length in base OT\n";
            exit(1);
        }
        receiver_procS(&receiver);
        receiver_maketable(&receiver);
    }

    os[0].reset_write_head();

    for (i=0;i<nOT;i++) printf("%d", receiver_inputs[i].get());

    for (i = 0; i < nOT; i += 4)
    {
	printf("\n\nnew receiver inputs? %d \n\n", new_receiver_inputs);
        if (ot_role & RECEIVER)
        {
            for (j = 0; j < 4; j++)
            {
                if (new_receiver_inputs)
                    receiver_inputs[i + j] = G.get_uchar()&1;
                cs[j] = receiver_inputs[i + j].get();
		printf ("%d\n", receiver_inputs[i+j].get());
            }
            receiver_rsgen(&receiver, Rs_pack[0], cs);
            os[0].store_bytes(Rs_pack[0], sizeof(Rs_pack[0]));
            receiver_keygen(&receiver, receiver_keys);

	    
            // Copy keys to receiver_outputs
	    for (j = 0; j < 4; j++)
            {
                for (k = 0; k < AES_BLK_SIZE; k++)
                {
                    receiver_outputs[i + j].set_byte(k, receiver_keys[j][k]);
                }
            }
		
	    
	    //for (int s=0; s<AES_BLK_SIZE;s++)
		//printf ("Receiver Keys:   %x\n", receiver_keys[i][s]);
	    	
	    
	    for (k=0; k<AES_BLK_SIZE; k++)
	    {
		    printf ("Receiver output OT n%d :  %x\n",i,receiver_outputs[i].get_byte(k));
		    //receiver_outputs[i].set_byte(k, receiver_out[k]);
	    }
	    printf("\n\n");

        }
    }

    send_if_ot_receiver(P, os, ot_role);
        
    for (i = 0; i < nOT; i += 4)
    {
        if (ot_role & SENDER)
        {
            os[1].get_bytes((octet*) Rs_pack[1], len);
            if (len != sizeof(Rs_pack[1]))
            {
                cerr << "Received invalid length in base OT\n";
                exit(1);
            }
            sender_keygen(&sender, Rs_pack[1], sender_keys);

            // Copy 128 bits of keys to sender_inputs
            for (j = 0; j < 4; j++)
            {
                for (k = 0; k < AES_BLK_SIZE; k++)
                {
                    sender_inputs[i + j][0].set_byte(k, sender_keys[0][j][k]);
                    sender_inputs[i + j][1].set_byte(k, sender_keys[1][j][k]);
                }
            }

	    
	    for (k=0; k<AES_BLK_SIZE; k++)
	    {
		    printf ("Sender output OT n%d :      %x           %x       diff: %d\n",i,sender_inputs[i][0].get_byte(k), sender_inputs[i][1].get_byte(k)
				    , (int)sender_inputs[i][0].get_byte(k) - (int)sender_inputs[i][1].get_byte(k));
		    //receiver_outputs[i].set_byte(k, receiver_out[k]);
	    }
	    printf("\n\n");


        }
        #ifdef BASE_OT_DEBUG
        for (j = 0; j < 4; j++)
        {
            if (ot_role & SENDER)
            {
                printf("%4d-th sender keys:", i+j);
                for (k = 0; k < HASHBYTES; k++) printf("%.2X", sender_keys[0][j][k]);
                printf(" ");
                for (k = 0; k < HASHBYTES; k++) printf("%.2X", sender_keys[1][j][k]);
                printf("\n");
            }
            if (ot_role & RECEIVER)
            {
                printf("%4d-th receiver key:", i+j);
                for (k = 0; k < HASHBYTES; k++) printf("%.2X", receiver_keys[j][k]);
                printf("\n");
            }
        }

        printf("\n");
        #endif
    }*/
    set_seeds();
}

void BaseOT::set_seeds()
{
    for (int i = 0; i < nOT; i++)
    {
        // Set PRG seeds
        if (ot_role & SENDER)
        {
            G_sender[i][0].SetSeed(sender_inputs[i][0].get_ptr());
            G_sender[i][1].SetSeed(sender_inputs[i][1].get_ptr());
        }
        if (ot_role & RECEIVER)
        {
            G_receiver[i].SetSeed(receiver_outputs[i].get_ptr());
        }
    }
    extend_length();
}

void BaseOT::extend_length()
{
    for (int i = 0; i < nOT; i++)
    {
        if (ot_role & SENDER)
        {
            sender_inputs[i][0].randomize(G_sender[i][0]);
            sender_inputs[i][1].randomize(G_sender[i][1]);
        }
        if (ot_role & RECEIVER)
        {
            receiver_outputs[i].randomize(G_receiver[i]);
        }
    }
}


void BaseOT::check()
{
    vector<octetStream> os(2);
    BitVector tmp_vector(8 * AES_BLK_SIZE);


    for (int i = 0; i < nOT; i++)
    {
        if (ot_role == SENDER)
        {
            // send both inputs over
            sender_inputs[i][0].pack(os[0]);
            sender_inputs[i][1].pack(os[0]);
            P->send(os[0]);
        }
        else if (ot_role == RECEIVER)
        {
            P->receive(os[1]);
        }
        else
        {
            // both sender + receiver
            sender_inputs[i][0].pack(os[0]);
            sender_inputs[i][1].pack(os[0]);
            P->send_receive_player(os);
        }
        if (ot_role & RECEIVER)
        {
            tmp_vector.unpack(os[1]);
        
            if (receiver_inputs[i] == 1)
            {
                tmp_vector.unpack(os[1]);
            }
            if (!tmp_vector.equals(receiver_outputs[i]))
            {
                cerr << "Incorrect OT\n";
                exit(1);
            }
        }
        os[0].reset_write_head();
        os[1].reset_write_head();
    }
}


void FakeOT::exec_base(bool new_receiver_inputs)
{
    insecure("base OTs");
    PRNG G;
    G.ReSeed();
    vector<octetStream> os(2);
    vector<BitVector> bv(2, 128);

    if ((ot_role & RECEIVER) && new_receiver_inputs)
    {
        for (int i = 0; i < nOT; i++)
            // Generate my receiver inputs
            receiver_inputs[i] = G.get_uchar()&1;
    }

    if (ot_role & SENDER)
        for (int i = 0; i < nOT; i++)
            for (int j = 0; j < 2; j++)
            {
                sender_inputs[i][j].randomize(G);
                sender_inputs[i][j].pack(os[0]);
            }

    send_if_ot_sender(P, os, ot_role);

    if (ot_role & RECEIVER)
        for (int i = 0; i < nOT; i++)
        {
            for (int j = 0; j < 2; j++)
                bv[j].unpack(os[1]);
            receiver_outputs[i] = bv[receiver_inputs[i].get()];
        }

    set_seeds();
}
