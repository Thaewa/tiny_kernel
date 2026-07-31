#include "osSemaphore.h"

/*-----------------------------------------------------------
 * osBinSemInit
 *
 * Initializes a binary semaphore before it is used by any
 * thread. A binary semaphore can represent only two states:
 * available (one) and unavailable (zero).
 *
 * Any positive initial value is normalized to one. Zero and
 * all negative values are normalized to zero. Normalization
 * prevents an invalid count from entering the binary
 * semaphore state.
 *
 * Parameters:
 *   sem     - Pointer to the semaphore object to initialize.
 *   initial - Requested initial availability value.
 *
 * Precondition:
 *   sem must point to valid writable storage. Null-pointer
 *   handling will be defined by a later kernel-wide error
 *   handling milestone.
 *----------------------------------------------------------*/
void osBinSemInit(osBinSem *sem, int32_t initial)
{
	/* Clamp the requested value to the binary range of zero or one. */
	sem->value = (initial > 0) ? 1 : 0;
}
