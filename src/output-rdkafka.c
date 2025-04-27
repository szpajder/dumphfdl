/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>                      // fprintf
#include <string.h>                     // strdup, strerror
#include <errno.h>                      // errno
#include <librdkafka/rdkafka.h>
#include "output-common.h"              // output_descriptor_t, output_qentry_t, output_queue_drain
#include "config.h"                     // Uhh
#include "kvargs.h"                     // kvargs
#include "options.h"                    // option_descr_t
#include "util.h"                       // ASSERT, NEW

typedef struct {
    char *brokers;
    char *topic;
    char *sasl_username;
    char *sasl_password;
    char *sasl_mechanism;
    char *security_protocol;
    void *rdkafka_ctx;
    void *rdkafka_sock;
    void *rk;
} out_rdkafka_ctx_t;

static bool out_rdkafka_supports_format(output_format_t format) {
    return(format == OFMT_TEXT || format == OFMT_BASESTATION || format == OFMT_JSON);
}

static void *out_rdkafka_configure(kvargs *kv) {
    ASSERT(kv != NULL);
    NEW(out_rdkafka_ctx_t, cfg);

    if(kvargs_get(kv, "brokers") == NULL) {
        fprintf(stderr, "output_rdkafka: brokers not specified\n");
        goto fail;
    }
    cfg->brokers = strdup(kvargs_get(kv, "brokers"));
    fprintf(stderr, "output_rdkafka: Configuring broker: %s\n", cfg->brokers);

    if(kvargs_get(kv, "topic") == NULL) {
        fprintf(stderr, "output_rdkafka: topic not specified\n");
        goto fail;
    }
    cfg->topic = strdup(kvargs_get(kv, "topic"));

    if(kvargs_get(kv, "sasl_username") != NULL &&
       kvargs_get(kv, "sasl_password") != NULL &&
       kvargs_get(kv, "sasl_mechanism") != NULL &&
       kvargs_get(kv, "security_protocol") != NULL) {
       // Enable SASL authentication
      cfg->sasl_username = strdup(kvargs_get(kv, "sasl_username"));
      cfg->sasl_password = strdup(kvargs_get(kv, "sasl_password"));
      cfg->sasl_mechanism = strdup(kvargs_get(kv, "sasl_mechanism"));
      cfg->security_protocol = strdup(kvargs_get(kv, "security_protocol"));
    }

    return cfg;
fail:
    XFREE(cfg);
    return NULL;
}

static int out_rdkafka_init(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;

    rd_kafka_conf_t *conf = rd_kafka_conf_new();

    char errstr[512];

    if (rd_kafka_conf_set(conf, "bootstrap.servers", self->brokers,
                          errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
      fprintf(stderr, "%% %s\n", errstr);
      exit(1);
    }

    rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();

    if (rd_kafka_topic_conf_set(topic_conf, "acks", "all",
                          errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
      fprintf(stderr, "%% %s\n", errstr);
      exit(1);
    }

    // Enable authentication if configured
    if (self->sasl_username != NULL &&
        self->sasl_password != NULL &&
        self->sasl_mechanism != NULL &&
        self->security_protocol != NULL) {
          if (rd_kafka_conf_set(conf, "sasl.mechanism", self->sasl_mechanism,
                            errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
              fprintf(stderr, "%% %s\n", errstr);
              exit(1);
          }
          if (rd_kafka_conf_set(conf, "sasl.username", self->sasl_username,
                            errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
              fprintf(stderr, "%% %s\n", errstr);
              exit(1);
          }
          if (rd_kafka_conf_set(conf, "sasl.password", self->sasl_password,
                            errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
              fprintf(stderr, "%% %s\n", errstr);
              exit(1);
          }
          if (rd_kafka_conf_set(conf, "security.protocol", self->security_protocol,
                            errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
              fprintf(stderr, "%% %s\n", errstr);
              exit(1);
          }
    }

    /* Create Kafka producer handle */
	  fprintf(stderr, "output_rdkafka(%s): creating producer...\n", self->brokers);
    if (!(self->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                            errstr, sizeof(errstr)))) {
      fprintf(stderr, "%% Failed to create new producer: %s\n", errstr);
      exit(1);
    }

	  fprintf(stderr, "output_rdkafka(%s): connecting...\n", self->brokers);
    self->rdkafka_ctx = rd_kafka_new(RD_KAFKA_PRODUCER,
            conf,
            errstr,
            sizeof(errstr));

    if(self->rdkafka_ctx == NULL) {
        fprintf(stderr, "output_rdkafka(%s): failed to set up Kafka producer context\n", self->brokers);
        return -1;
    }
	  fprintf(stderr, "output_rdkafka(%s): connection established\n", self->brokers);
    return 0;
}

static void out_rdkafka_produce_text(out_rdkafka_ctx_t *self, struct metadata *metadata, struct octet_string *msg) {
    UNUSED(metadata);
    ASSERT(msg != NULL);

    long delivery_counter;

    if(msg->len < 2) {
        return;
    }

	  fprintf(stderr, "output_rdkafka(%s): producing message...\n", self->brokers);

    rd_kafka_resp_err_t err;

    err = rd_kafka_producev(
        self->rk,
        RD_KAFKA_V_TOPIC(self->topic),
        RD_KAFKA_V_KEY(NULL, 0),
        RD_KAFKA_V_VALUE(msg->buf, msg->len),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_OPAQUE(&delivery_counter),
        RD_KAFKA_V_END);
    if (err) {
        fprintf(stderr, "Produce failed: %s\n",
                rd_kafka_err2str(err));
    }

    /* Poll for delivery report callbacks to know the final
     * delivery status of previously produced messages. */
    rd_kafka_poll(self->rk, 0);
}

static int out_rdkafka_produce(void *selfptr, output_format_t format, struct metadata *metadata, struct octet_string *msg) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;
    if(format == OFMT_TEXT || format == OFMT_JSON || format == OFMT_BASESTATION) {
        out_rdkafka_produce_text(self, metadata, msg);
    }
    return 0;
}

static void out_rdkafka_handle_shutdown(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;
    fprintf(stderr, "output_rdkafka(%s): shutting down\n", self->brokers);
    rd_kafka_destroy(self->rk);
}

static void out_rdkafka_handle_failure(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;
    fprintf(stderr, "output_rdkafka(%s): deactivating output\n",
            self->brokers);
}

static const option_descr_t out_rdkafka_options[] = {
    {
        .name= "brokers",
        .description = "Kafka Brokers (broker0:9092,broker1:9092,...) (required)"
    },
    {
        .name= "topic",
        .description = "Kafka topic (required)"
    },
    {
        .name = NULL,
        .description = NULL
    }
};

output_descriptor_t out_DEF_rdkafka = {
    .name = "rdkafka",
    .description = "Output to an Apache Kafka broker",
    .options = out_rdkafka_options,
    .supports_format = out_rdkafka_supports_format,
    .configure = out_rdkafka_configure,
    .init = out_rdkafka_init,
    .produce = out_rdkafka_produce,
    .handle_shutdown = out_rdkafka_handle_shutdown,
    .handle_failure = out_rdkafka_handle_failure
};
