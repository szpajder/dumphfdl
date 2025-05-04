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
    char *acks;
    bool verbose_log;
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

    if (kvargs_get(kv, "acks") != NULL) {
      cfg->acks = strdup(kvargs_get(kv, "acks"));
    } else {
      cfg->acks = "all";
    }

    // Output a log message to stderr for each message produced to Kafka
    if (kvargs_get(kv, "verbose_log") != NULL &&
        strcmp(strdup(kvargs_get(kv, "verbose_log")), "true") == 0)  {
      fprintf(stderr, "output_rdkafka: verbose kafka logging enabled\n");
      cfg->verbose_log = true;
    } else {
      cfg->verbose_log = false;
    }

    return cfg;
fail:
    XFREE(cfg);
    return NULL;
}

static void rdkafka_conf_set(rd_kafka_conf_t *conf, char *key, char *val) {
  ASSERT(conf != null);

  char errstr[512];
  if (rd_kafka_conf_set(conf, key, val,
                        errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
    fprintf(stderr, "%% rdkafka config error: %s\n", errstr);
    exit(1);
  }
}

static int out_rdkafka_init(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;

    rd_kafka_conf_t *conf = rd_kafka_conf_new();

    char errstr[512];

    rdkafka_conf_set(conf, "bootstrap.servers", self->brokers);
    rdkafka_conf_set(conf, "acks", self->acks);

    // Enable authentication if configured
    if (self->sasl_username != NULL &&
        self->sasl_password != NULL &&
        self->sasl_mechanism != NULL &&
        self->security_protocol != NULL) {
          rdkafka_conf_set(conf, "sasl.mechanism", self->sasl_mechanism);
          rdkafka_conf_set(conf, "sasl.username", self->sasl_username);
          rdkafka_conf_set(conf, "sasl.password", self->sasl_password);
          rdkafka_conf_set(conf, "security.protocol", self->security_protocol);
    }

    /* Create Kafka producer handle */
	  fprintf(stderr, "output_rdkafka(%s): creating producer...\n", self->brokers);
    if (!(self->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                            errstr, sizeof(errstr)))) {
      fprintf(stderr, "%% Failed to create new producer: %s\n", errstr);
      exit(1);
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

    rd_kafka_resp_err_t err;

    if (self->verbose_log) {
      fprintf(stderr, "output_rdkafka: producing message to kafka topic %s", self->topic);
    }

    err = rd_kafka_producev(
        self->rk,
        RD_KAFKA_V_TOPIC(self->topic),
        RD_KAFKA_V_KEY(NULL, 0),
        RD_KAFKA_V_VALUE(msg->buf, msg->len),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_OPAQUE(&delivery_counter),
        RD_KAFKA_V_END);
    if (err) {
        fprintf(stderr, "output_rdkafka: Produce failed: %s\n",
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
        .name= "sasl_username",
        .description = "SASL Username"
    },
    {
        .name= "sasl_password",
        .description = "SASL Password"
    },
    {
        .name= "sasl_mechanism",
        .description = "SASL Mechanism - Accepted values: PLAIN, SCRAM-SHA-256, SCRAM-SHA-512"
    },
    {
        .name= "security_protocol",
        .description = "Security Protocol - Accepted values: plaintext, ssl, sasl_plaintext, sasl_ssl"
    },
    {
        .name= "acks",
        .description = "Required number of acks - Default: all"
    },
    {
        .name= "verbose_log",
        .description = "Print verbose log messages for each produced message - Default: false"
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
