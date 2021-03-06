import threading
import pika
from termcolor import colored


class Consumer:
    """
    Implements basic communication setup and message receiving methods.
    """

    def __init__(self, exchange_name, exchange_type, connection_address):
        """
        Creates initial setup.
        """
        self._exchange_name = exchange_name
        self._connection = pika.BlockingConnection(
            pika.ConnectionParameters(host=connection_address, heartbeat=30))
        self._channel = self._connection.channel()
        self._channel.exchange_declare(exchange=exchange_name,
                                       exchange_type=exchange_type,
                                       auto_delete=True)
        self._channel.basic_qos(prefetch_count=1)
        self._queues = []

    def add_queue(self, queue_name='', routing_key=None, callback=None):
        """
        Declares new queue, binds given key to it, sets
        callback for messages consuming.

        Returns the name of the created queue.
        """
        self._queues.append(queue_name)
        result = self._channel.queue_declare(queue=queue_name, auto_delete=True)
        queue_name = result.method.queue

        self._channel.queue_bind(
            exchange=self._exchange_name,
            queue=queue_name,
            routing_key=routing_key
        )

        self._channel.basic_consume(
            queue=queue_name,
            on_message_callback=callback
        )

        return queue_name

    def start(self, new_thread=False):
        """
        Starts consuming messages from previously set queue.
        """
        if new_thread:
            thread = threading.Thread(target=self._channel.start_consuming,
                                      args=())
            thread.start()

        else:
            self._channel.start_consuming()

    def close(self):
        self._channel.close()


class Producer:
    """
    Implements basic functionality for sending messages.
    """

    def __init__(self, exchange_name, exchange_type, connection_address):
        """
        Creates initial setup.
        """
        self._exchange_name = exchange_name
        self._connection = pika.BlockingConnection(
            pika.ConnectionParameters(host=connection_address, heartbeat=30))
        self._channel = self._connection.channel()
        self._channel.exchange_declare(exchange=exchange_name,
                                       exchange_type=exchange_type,
                                       auto_delete=True)

    def send_message(self, routing_key='', message=None, **opts):
        """
        Sends given message to previously initialized exchange.
            - opts: options passed to basic_publish method
        """
        self._channel.basic_publish(
            exchange=self._exchange_name,
            routing_key=routing_key,
            body=message,
            **opts
        )


class HospitalWorker:
    """
    Represents any worker that can receive info messages from admins.
    """

    def __init__(self):
        """
        Initializes structures responsible for receiving admin info
        and publishing any messages.
        """
        self._info_consumer = Consumer('info', 'fanout', 'localhost')
        self._info_queue = self._info_consumer.add_queue(
            callback=self.process_info
        )
        self._info_consumer.start(new_thread=True)

        self._producer = Producer('hospital', 'topic', 'localhost')

    def process_info(self, ch, method, properties, body):
        """
        Callback for handling messages received from admins.
        """
        body = body.decode()
        log = colored('INFO: ' + body, 'red')
        print(log)
        ch.basic_ack(delivery_tag=method.delivery_tag)
