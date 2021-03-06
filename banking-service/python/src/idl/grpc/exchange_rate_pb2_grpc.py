# Generated by the gRPC Python protocol compiler plugin. DO NOT EDIT!
import grpc

import exchange_rate_pb2 as exchange__rate__pb2


class ExchangeRatesProviderStub(object):
  """The exchange rate service definition.
  """

  def __init__(self, channel):
    """Constructor.

    Args:
      channel: A grpc.Channel.
    """
    self.subscribe = channel.unary_stream(
        '/ExchangeRatesProvider/subscribe',
        request_serializer=exchange__rate__pb2.Subscription.SerializeToString,
        response_deserializer=exchange__rate__pb2.RatesUpdate.FromString,
        )


class ExchangeRatesProviderServicer(object):
  """The exchange rate service definition.
  """

  def subscribe(self, request, context):
    """Provides initial info and consecutive updates.
    """
    context.set_code(grpc.StatusCode.UNIMPLEMENTED)
    context.set_details('Method not implemented!')
    raise NotImplementedError('Method not implemented!')


def add_ExchangeRatesProviderServicer_to_server(servicer, server):
  rpc_method_handlers = {
      'subscribe': grpc.unary_stream_rpc_method_handler(
          servicer.subscribe,
          request_deserializer=exchange__rate__pb2.Subscription.FromString,
          response_serializer=exchange__rate__pb2.RatesUpdate.SerializeToString,
      ),
  }
  generic_handler = grpc.method_handlers_generic_handler(
      'ExchangeRatesProvider', rpc_method_handlers)
  server.add_generic_rpc_handlers((generic_handler,))
