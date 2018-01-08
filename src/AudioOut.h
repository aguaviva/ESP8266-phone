#include "openlpc.h"

void aoBegin(int samplingRate);
void aoEnd();
int16_t aoQueue(int16_t *data, int len);

void PlayHolaRaw();
void PlayHolaRawBuf();
void PlayHolaLPC(openlpc_decoder_state *st);
void PlaySin();
