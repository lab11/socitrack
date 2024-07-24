from processing import *
from dataclasses import dataclass, asdict
import struct
import numpy as np
import matplotlib.pyplot as plt

GYRO_DATA,ACC_DATA,LACC_DATA,GACC_DATA,QUAT_DATA,STAT_DATA = 0,1,2,3,4,5

data_type_len = {GYRO_DATA:6,ACC_DATA:6,LACC_DATA:6,GACC_DATA:6,QUAT_DATA:8,STAT_DATA:1}

@dataclass
class gyro:
    x: float
    y: float
    z: float

@dataclass
class quat:
    w: float
    x: float
    y: float
    z: float

@dataclass
class lacc:
    x: float
    y: float
    z: float

@dataclass
class stat:
    mag: int
    acc: int
    gyro: int  
    sys: int

GACC_SCALE_FACTOR = 100.0 #m/s^2
QUAT_SCALE_FACTOR = float((1 << 14))
LACC_SCALE_FACTOR = 100.0 #m/s^2
GYRO_SCALE_FACTOR = 16.0 #degree/s
ACC_SCALE_FACTOR = 100.0 #m/s^2

def unpack_imu_data_single_type(data_single, data_type):
    if data_type == GYRO_DATA:
        gx,gy,gz = struct.unpack('<3h',data_single)
        return gyro(gx/GYRO_SCALE_FACTOR,gy/GYRO_SCALE_FACTOR,gz/GYRO_SCALE_FACTOR)
    if data_type == QUAT_DATA:
        qw,qx,qy,qz = struct.unpack('<4h',data_single)
        return quat(qw/QUAT_SCALE_FACTOR,qx/QUAT_SCALE_FACTOR,qy/QUAT_SCALE_FACTOR,qz/QUAT_SCALE_FACTOR)
    if data_type == LACC_DATA:
        laccx,laccy,laccz = struct.unpack('<3h',data_single)
        return lacc(laccx/LACC_SCALE_FACTOR ,laccy/LACC_SCALE_FACTOR ,laccz/LACC_SCALE_FACTOR)
    if data_type == ACC_DATA:
        accx,accy,accz = struct.unpack('<3h',data_single)
        return acc(accx/LACC_SCALE_FACTOR ,accy/LACC_SCALE_FACTOR ,accz/LACC_SCALE_FACTOR)
    if data_type == STAT_DATA:
        reg_value = data_single[0]
        calib_mag = reg_value & 0x03
        calib_accel = (reg_value >> 2) & 0x03
        calib_gyro = (reg_value >> 4) & 0x03
        calib_sys = (reg_value >> 6) & 0x03
        return stat(calib_mag,calib_accel,calib_gyro,calib_sys)

def dataclass_to_list(d):
    return list(asdict(d).values())

def unpack_imu_data(data, data_type_seq):
    """
    unpack multi-byte multi-type imu data according to seq defined in data_type_seq
    returns an array with all elements in the order of data_type_seq
    the available types are defined in data_type_len dict
    """
    all_elements = []
    current_index = 0
    for t in data_type_seq:
        unpacked = unpack_imu_data_single_type(data[current_index:current_index+data_type_len[t]],t)
        current_index+=data_type_len[t]
        all_elements = all_elements + dataclass_to_list(unpacked)
    return all_elements

def generate_headers_formats(data_types):
    headers = ["timestamp"]
    formats = ['%.2f']

    for data_type in data_types:
        if data_type == GYRO_DATA:
            headers = headers + ["gyro_x","gyro_y","gyro_z"]
            formats+=["%.4f"]*3
        if data_type == STAT_DATA:
            headers = headers + ["calib_mag","calib_accel","calib_gyro","calib_sys"]
            formats+=["%d"]*4
        if data_type == LACC_DATA:
            headers = headers + ["lacc_x","lacc_y","lacc_z"]
            formats+=["%.4f"]*3
        if data_type == ACC_DATA:
            headers = headers + ["acc_x","acc_y","acc_z"]
            formats+=["%.4f"]*3
        if data_type == QUAT_DATA:
            headers = headers + ["quat_w","quat_x","quat_y","quat_z"]
            formats+=["%.14f"]*4
    return headers,formats

data_types = [STAT_DATA,LACC_DATA,GYRO_DATA,QUAT_DATA]
headers,formats = generate_headers_formats(data_types)
IMU_DATA_LEN = sum([data_type_len[x] for x in data_types])

if __name__ == "__main__":
    #a = load_data("./0_Yankee_doodle_Saloon_style_padded_100.pkl")
    a = load_data("./Unknown.pkl")
    all_data = []
    for segment in a.loc["i"]:
        if not isinstance(segment, list):
            print("NAN") # TODO: investigate why NAN shows up? it never happens in the past
            continue
        for ts, data in segment:
            try:
                unpacked = unpack_imu_data(data,data_types)
                #print(ts, unpacked)
                all_data.append([ts]+unpacked)
            except:
                print(ts)

    all_data = np.array(all_data)
    print(all_data)
    np.savetxt("output.csv", all_data, delimiter=',', header=','.join(headers), comments='', fmt=formats)

    timestamps = all_data[:, 0]
    timestamps = timestamps - timestamps[0]
    lacc_x =all_data[:, 5]
    lacc_y =all_data[:, 6]
    lacc_z =all_data[:, 7]
    gyro_x =all_data[:, 8]
    gyro_y =all_data[:, 9]
    gyro_z =all_data[:, 10]

    #plt.plot(timestamps, lacc_x)
    plt.plot(timestamps, lacc_y)
    plt.show()