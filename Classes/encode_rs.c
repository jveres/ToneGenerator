
#include <string.h>
#include "char.h"

void ENCODE_RS(void *p,DTYPE *data, DTYPE *bb)
{
  struct rs *rs = (struct rs *)p;
  int i, j;
  DTYPE feedback;

  memset(bb,0,NROOTS*sizeof(DTYPE));

  for(i=0;i<NN-NROOTS;i++){
    feedback = INDEX_OF[data[i] ^ bb[0]];
    if(feedback != A0){      /* feedback term is non-zero */
      for(j=1;j<NROOTS;j++)
	bb[j] ^= ALPHA_TO[MODNN(feedback + GENPOLY[NROOTS-j])];
    }
    /* Shift */
    memmove(&bb[0],&bb[1],sizeof(DTYPE)*(NROOTS-1));
    if(feedback != A0)
      bb[NROOTS-1] = ALPHA_TO[MODNN(feedback + GENPOLY[0])];
    else
      bb[NROOTS-1] = 0;
  }
}
