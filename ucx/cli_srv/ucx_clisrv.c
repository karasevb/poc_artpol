#include <stdio.h>
#include <poll.h>
#include <ucp/api/ucp.h>
#include <ucp/api/ucp_def.h>
#include <mpi.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern int errno;
#include <errno.h>

int rank, size;
int nmessages = 5;

void basta()
{
        int delay = 0;
        while( delay ){
            sleep(1);
        }

	MPI_Abort(MPI_COMM_WORLD, 0);
	exit(0);
}

// http://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
void hexDump (char *desc, void *addr, int len) {
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;
    if (desc != NULL)
        printf ("%s:\n", desc);

    if (len == 0) {
        printf("  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        printf("  NEGATIVE LENGTH: %i\n",len);
        return;
    }
    for (i = 0; i < len; i++) {
        if ((i % 16) == 0) {
            if (i != 0)
                printf ("  %s\n", buff);
            printf ("  %04x ", i);
        }
        printf (" %02x", pc[i]);
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }
    printf ("  %s\n", buff);
}

/* UCP handler objects */
ucp_context_h ucp_context;
ucp_worker_h ucp_worker;

typedef struct ucx_ep_t {
    ucp_address_t *server_addr;
    size_t server_addr_len;
} ucx_ep_t;

/* Endpoints array */
ucx_ep_t *ucx_ep = NULL;
char *addr_arr = NULL;

static ucp_address_t *ucx_addr_arr;

struct ucx_context {
    int completed;
};

struct test_message {
    int rank;
    int str[5];
};

struct ucx_context request;

#define EP_ADDRESS(_rank) ucx_ep[_rank].server_addr
#define EP_SIZE(_rank) ucx_ep[_rank].server_addr_len

static void request_init(void *request)
{
    struct ucx_context *ctx = (struct ucx_context *) request;
    ctx->completed = 0;
}

static void send_handle(void *request, ucs_status_t status)
{
    struct ucx_context *context = (struct ucx_context *) request;
    context->completed = 1;
}

static void recv_handle(void *request, ucs_status_t status,
                       ucp_tag_recv_info_t *info)
{
    struct ucx_context *context = (struct ucx_context *) request;
    context->completed = 1;
}

int prepare_ucx()
{
    ucp_config_t *config;
    ucs_status_t status;
    ucp_params_t ucp_params;
    ucp_worker_params_t worker_params;
    ucp_address_t *server_addr;
    size_t server_addr_len;


    unsigned long len = server_addr_len;
    static int *addr_len_arr = NULL;
    unsigned long *_addr_len_arr = NULL;
    int *offsets = NULL;
    int i;
    
    status = ucp_config_read(NULL, NULL, &config);
    if (status != UCS_OK) {
        basta();
    }

    ucp_params.features = UCP_FEATURE_TAG; /*| UCP_FEATURE_WAKEUP;*/
    ucp_params.request_size    = sizeof(struct ucx_context);
    ucp_params.request_init    = request_init;
    ucp_params.request_cleanup = NULL;
    ucp_params.field_mask      = UCP_PARAM_FIELD_FEATURES |
                             UCP_PARAM_FIELD_REQUEST_SIZE |
                             UCP_PARAM_FIELD_REQUEST_INIT |
                             UCP_PARAM_FIELD_REQUEST_CLEANUP;
    
    status = ucp_init(&ucp_params, config, &ucp_context);
//    ucp_config_print(config, stdout, NULL, UCS_CONFIG_PRINT_CONFIG);
    ucp_config_release(config);
    if (status != UCS_OK) {
        basta();
    }
    
    worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;

    status = ucp_worker_create(ucp_context, &worker_params, &ucp_worker);
    if (status != UCS_OK) {
        basta();
    }

    status = ucp_worker_get_address(ucp_worker, &server_addr, &server_addr_len);
    if (status != UCS_OK) {
        basta();
    }
    len = server_addr_len;

    _addr_len_arr = (unsigned long*)malloc(sizeof(unsigned long) * size);
    MPI_Allgather(&len, 1, MPI_UNSIGNED_LONG, _addr_len_arr, 1, MPI_UNSIGNED_LONG, MPI_COMM_WORLD);

    if (rank == 0){
        int i;
        for (i = 0; i < size; i++) {
            printf("%d addr_len %lu\n", i, _addr_len_arr[i]);
        }
    }

    offsets = (int *)malloc(sizeof(int) * size);
    addr_len_arr = (int*)malloc(sizeof(int) * size);
    for(i = 0;i < size; i++) {
        addr_len_arr[i] = (int)_addr_len_arr[i];
        if (rank == 0) {
            printf("%d %d\n", i, addr_len_arr[i]);
        }
    }

    offsets[0] = 0;
    for(i = 1; i < size; i++) {
        offsets [i] = offsets[i-1] + (int)addr_len_arr[i-1];
    }
    addr_arr = (char *) malloc(sizeof(char) * (offsets [size-1] + (int)addr_len_arr[size-1]));

    if (rank == 0) {
        for(i=0; i<size; i++) {
            printf("%d\n", offsets[i]);
        }
    }
    MPI_Allgatherv(server_addr, (int)addr_len_arr[rank], MPI_CHAR, (void*)addr_arr, addr_len_arr, offsets, MPI_CHAR, MPI_COMM_WORLD);

    ucp_worker_release_address(ucp_worker, server_addr);

    ucx_ep = (ucx_ep_t*) malloc(sizeof(ucx_ep_t) * size);
    for (i = 0; i < size; i++) {
        ucx_ep[i].server_addr_len = _addr_len_arr[i];
        ucx_ep[i].server_addr = (ucp_address_t*)(addr_arr + offsets[i]);
        if (rank == 0) {
            char str[32];
            sprintf(str, "ep %d", i);
            hexDump(str, ucx_ep[i].server_addr, ucx_ep[i].server_addr_len);
        }
    }
    free(_addr_len_arr);
    free(addr_len_arr);
    free(offsets);
}

void cleanup_ucx()
{
    //ucp_worker_release_address(ucp_worker, ucx_ep[rank].server_addr);
    ucp_worker_destroy(ucp_worker);
    ucp_cleanup(ucp_context);
    free(addr_arr);
}

void client_operation()
{
    ucs_status_t status;
    struct test_message msg;
    ucp_ep_params_t ep_params;
    ucp_ep_h server_ep;
    struct ucx_context *request = 0;
    int i;

    /* Send client UCX address to server */
    ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
    ep_params.address    = EP_ADDRESS(0);

    status = ucp_ep_create(ucp_worker, &ep_params, &server_ep);
    if (status != UCS_OK) {
        basta();
    }

    msg.rank = rank;
    for(i = 0; i < 5; i++){
        int j;
        for(j=0; j< 5; j++){
            msg.str[j] = i * 5 + j;
        }
        request = ucp_tag_send_nb(server_ep, (void*)&msg, sizeof(msg),
                                    ucp_dt_make_contig(1), 1, send_handle);
        if (UCS_PTR_IS_ERR(request)) {
            printf("%d: unable to send UCX address message\n", rank);
            basta();
        } else if (UCS_PTR_STATUS(request) != UCS_OK) {
            while( request->completed == 0 ){
                 ucp_worker_progress(ucp_worker);
            }
            ucp_request_release(request);
        }
    }
}

void recv_message_handler(void *_msg) {
    struct test_message *msg = _msg;
    printf("[0x%x] I'm %d, message from %d\n", (unsigned int)pthread_self(), rank, msg->rank);
}

void * recv_listener_thread(void *args)
{
    int ranks_account[size - 1];
    ucs_status_t status;
    int fd, flag = 1;
    struct test_message msg;
    struct ucx_context *request = 0;
    ucp_tag_message_h msg_tag;
    ucp_tag_recv_info_t info_tag;
    int i, cur;

    memset(ranks_account, 0, sizeof(ranks_account));

    /* get fd to poll on */
    /*status = ucp_worker_get_efd(ucp_worker, &fd);
    if (status != UCS_OK) {
        basta();
    }*/

    do {
#if 0
        struct pollfd pfd = { fd, POLLIN, 0 };
        int rc;

        status = ucp_worker_arm(ucp_worker);
        if (status == UCS_ERR_BUSY) { /* some events are arrived already */
            goto process;
        }
        if( status != UCS_OK ){
            basta();
        }

        rc = poll(&pfd, 1, -1);

        if( rc < 0 ){
            if( errno == EINTR ){
                continue;
            } else {
                basta();
            }
        }
        if( !(pfd.revents & POLLIN) ){
            continue;
        }
#endif
process:
        do {
            int idx;
            ucp_worker_progress(ucp_worker);
            msg_tag = ucp_tag_probe_nb(ucp_worker,1, 0xffffffffffffffff, 1, &info_tag);

            if( NULL == msg_tag ){
                break;
            }
            if( info_tag.length != sizeof(struct test_message) ){
                printf("Bad message!\n");
                basta();
            }
            request = (struct ucx_context*)
                    ucp_tag_msg_recv_nb(ucp_worker, (void*)&msg, info_tag.length,
                                        ucp_dt_make_contig(1), msg_tag, recv_handle);
            while( request->completed == 0 ){
                ucp_worker_progress(ucp_worker);
            }
            ucp_request_release(request);

            recv_message_handler((void*)&msg);
            /*
            printf("Message from %d\n", msg.rank);
            for(idx = 0; idx < 5; idx++){
                if( msg.str[0] == idx * 5 ){
                    if( idx > ranks_account[msg.rank - 1] ){
                        printf("Out of order message from %d: expect %d, get %d\n",
                            msg.rank, ranks_account[msg.rank - 1], idx);
                    }
                    break;
                }
            }
            if( idx == 5 ){
                printf("%d: Unknown message from %d\n", rank, msg.rank);
                continue;
            }

            for(i=0; i < 5; i++){
                if( msg.str[i] != i + idx * 5 ){
                    printf("%d: Mismatch from rank %d\n", rank, msg.rank);
                }
            }
            ranks_account[msg.rank - 1]++;
            */
        } while( 1 );
        /* check the completion */
        flag = 0;
        for(i=0; i<size-1; i++){
            if( ranks_account[i] != 5 ){
                flag = 1;
                break;
            }
        }
    } while( flag );
}

int ucx_init(pthread_t *listener_thr) {

    pthread_create(listener_thr, NULL, recv_listener_thread, NULL);
    return 0;
}

int main(int argc, char **argv)
{
    pthread_t listener_thr;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    if( 0 != rank ){
        int delay = 0;
        while( delay ){
            sleep(1);
        }
    }

    prepare_ucx();

    if( 0 == rank ){
        ucx_init(&listener_thr);
        //server_operation();
        printf("SUCCESS\n");
        pthread_join(listener_thr, NULL);
    } else {
        client_operation();
    }
    cleanup_ucx();
    MPI_Finalize();
}
