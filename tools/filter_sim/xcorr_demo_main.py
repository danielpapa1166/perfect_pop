import numpy as np
import matplotlib.pyplot as plt


def xcorr_filter(signal, filter_window): 
    return np.correlate(signal, filter_window, mode='full')


def my_xcorr(signal, filter_window):
    result = np.zeros(len(signal))
    filter_size = len(filter_window)
    for i in range(len(signal)): 
        for j in range(len(filter_window)):
            if(i - j >= 0):
                # Perform the correlation operation
                result[i] += signal[i - j] * filter_window[filter_size - 1 - j]
    return result


if __name__ == "__main__":
    signal = np.array([0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0])
    filter_window = np.array([1, 2, 3])
    result = xcorr_filter(signal, filter_window)
    print(result)


    result_my = my_xcorr(signal, filter_window)
    print(result_my)
    plt.stem(result, 'b')
    plt.stem(result_my, 'g')
    plt.show()