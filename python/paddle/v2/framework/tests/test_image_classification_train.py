import paddle.v2 as paddle
import paddle.v2.framework.layers as layers
import paddle.v2.framework.nets as nets
import paddle.v2.framework.core as core
import paddle.v2.framework.optimizer as optimizer

from paddle.v2.framework.framework import Program, g_program
from paddle.v2.framework.executor import Executor

import numpy as np


def resnet_cifar10(input, depth=32, program=None, init_program=None):
    def conv_bn_layer(input,
                      ch_out,
                      filter_size,
                      stride,
                      padding,
                      act='relu',
                      program=None,
                      init_program=None):
        tmp = layers.conv2d(
            input=input,
            filter_size=filter_size,
            num_filters=ch_out,
            stride=stride,
            padding=padding,
            act=None,
            bias_attr=False,
            program=program,
            init_program=init_program)
        return layers.batch_norm(
            input=tmp, act=act, program=program, init_program=init_program)

    def shortcut(input, ch_in, ch_out, stride, program, init_program):
        if ch_in != ch_out:
            return conv_bn_layer(input, ch_out, 1, stride, 0, None, program,
                                 init_program)
        else:
            return input

    def basicblock(input,
                   ch_in,
                   ch_out,
                   stride,
                   program=program,
                   init_program=init_program):
        tmp = conv_bn_layer(
            input,
            ch_out,
            3,
            stride,
            1,
            program=program,
            init_program=init_program)
        tmp = conv_bn_layer(
            tmp,
            ch_out,
            3,
            1,
            1,
            act=None,
            program=program,
            init_program=init_program)
        short = shortcut(input, ch_in, ch_out, stride, program, init_program)
        return layers.elementwise_add(
            x=tmp,
            y=short,
            act='relu',
            program=program,
            init_program=init_program)

    def layer_warp(block_func, input, ch_in, ch_out, count, stride, program,
                   init_program):
        tmp = block_func(input, ch_in, ch_out, stride, program, init_program)
        for i in range(1, count):
            tmp = block_func(tmp, ch_out, ch_out, 1, program, init_program)
        return tmp

    assert (depth - 2) % 6 == 0
    n = (depth - 2) / 6
    conv1 = conv_bn_layer(
        input=input,
        ch_out=16,
        filter_size=3,
        stride=1,
        padding=1,
        program=program,
        init_program=init_program)
    res1 = layer_warp(
        basicblock,
        conv1,
        16,
        16,
        n,
        1,
        program=program,
        init_program=init_program)
    res2 = layer_warp(
        basicblock,
        res1,
        16,
        32,
        n,
        2,
        program=program,
        init_program=init_program)
    res3 = layer_warp(
        basicblock,
        res2,
        32,
        64,
        n,
        2,
        program=program,
        init_program=init_program)
    pool = layers.pool2d(
        input=res3,
        pool_size=8,
        pool_type='avg',
        pool_stride=1,
        program=program,
        init_program=init_program)
    return pool


def vgg16_bn_drop(input, program, init_program):
    def conv_block(input,
                   num_filter,
                   groups,
                   dropouts,
                   program=None,
                   init_program=None):
        return nets.img_conv_group(
            input=input,
            pool_size=2,
            pool_stride=2,
            conv_num_filter=[num_filter] * groups,
            conv_filter_size=3,
            conv_act='relu',
            conv_with_batchnorm=True,
            conv_batchnorm_drop_rate=dropouts,
            pool_type='max',
            program=program,
            init_program=init_program)

    conv1 = conv_block(input, 64, 2, [0.3, 0], program, init_program)
    conv2 = conv_block(conv1, 128, 2, [0.4, 0], program, init_program)
    conv3 = conv_block(conv2, 256, 3, [0.4, 0.4, 0], program, init_program)
    conv4 = conv_block(conv3, 512, 3, [0.4, 0.4, 0], program, init_program)
    conv5 = conv_block(conv4, 512, 3, [0.4, 0.4, 0], program, init_program)

    drop = layers.dropout(
        x=conv5, dropout_prob=0.5, program=program, init_program=init_program)
    fc1 = layers.fc(input=drop,
                    size=512,
                    act=None,
                    program=program,
                    init_program=init_program)
    reshape1 = layers.reshape(
        x=fc1,
        shape=list(fc1.shape + (1, 1)),
        program=program,
        init_program=init_program)
    bn = layers.batch_norm(
        input=reshape1, act='relu', program=program, init_program=init_program)
    drop2 = layers.dropout(
        x=bn, dropout_prob=0.5, program=program, init_program=init_program)
    fc2 = layers.fc(input=drop2,
                    size=512,
                    act=None,
                    program=program,
                    init_program=init_program)
    return fc2


init_program = Program()
program = Program()

classdim = 10
data_shape = [3, 32, 32]

images = layers.data(
    name='pixel', shape=data_shape, data_type='float32', program=program)

label = layers.data(
    name='label',
    shape=[1],
    data_type='int64',
    program=program,
    init_program=init_program)

# Add neural network config
# option 1. resnet
net = resnet_cifar10(images, 32, program, init_program)
# option 2. vgg
# net = vgg16_bn_drop(images, program, init_program)

# print(program)

predict = layers.fc(input=net,
                    size=classdim,
                    act='softmax',
                    program=program,
                    init_program=init_program)
cost = layers.cross_entropy(
    input=predict, label=label, program=program, init_program=init_program)
avg_cost = layers.mean(x=cost, program=program, init_program=init_program)

sgd_optimizer = optimizer.SGDOptimizer(learning_rate=0.001)
opts = sgd_optimizer.minimize(avg_cost, init_program)

BATCH_SIZE = 128
PASS_NUM = 1

train_reader = paddle.batch(
    paddle.reader.shuffle(
        paddle.dataset.cifar.train10(), buf_size=128 * 10),
    batch_size=BATCH_SIZE)

place = core.CPUPlace()
exe = Executor(place)

exe.run(init_program, feed={}, fetch_list=[])

for pass_id in range(PASS_NUM):
    batch_id = 0
    for data in train_reader():
        img_data = np.array(map(lambda x: x[0].reshape(data_shape),
                                data)).astype("float32")
        y_data = np.array(map(lambda x: x[1], data)).astype("int64")
        batch_size = 1
        for i in y_data.shape:
            batch_size = batch_size * i
        y_data = y_data.reshape([batch_size, 1])

        tensor_img = core.LoDTensor()
        tensor_y = core.LoDTensor()
        tensor_img.set(img_data, place)
        tensor_y.set(y_data, place)

        outs = exe.run(program,
                       feed={"pixel": tensor_img,
                             "label": tensor_y},
                       fetch_list=[avg_cost])

        loss = np.array(outs[0])
        print("pass_id:" + str(pass_id) + " batch_id:" + str(batch_id) +
              " loss:" + str(loss))
        batch_id = batch_id + 1

        if batch_id > 1:
            # this model is slow, so if we can train two mini batch, we think it works properly.
            exit(0)
exit(1)
