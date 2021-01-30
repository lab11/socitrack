import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.neighbors import KNeighborsClassifier
from sklearn.metrics import accuracy_score


def train_knn(X, Y):
    """Trains a K-nearest-neighbors classifier on data X and labels Y, and returns the classifier"""

    X = np.array(X)
    Y = np.array(Y)

    x_train, x_test, y_train, y_test = train_test_split(X,Y, train_size=0.625)

    x_train = np.array(x_train)
    y_train = np.array(y_train)
    x_test = np.array(x_test)
    y_test = np.array(y_test)

    clf = KNeighborsClassifier(n_neighbors = 1)
    clf.fit(x_train, y_train)

    y_pred = clf.predict(x_test)
    print(accuracy_score(y_test, y_pred))

    return clf