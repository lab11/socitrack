import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.neighbors import KNeighborsClassifier
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score


def train_knn(X, Y):
    """Trains a K-nearest-neighbors classifier on data X and labels Y, and returns the classifier"""
    x_train = np.array(X)
    y_train = np.array(Y)

    clf = KNeighborsClassifier(n_neighbors = 1)
    clf.fit(x_train, y_train)

    return clf


def train_forest(X, Y):
    """Trains a Random Forest classifier on data X and labels Y, and returns the classifier"""

    x_train = np.array(X)
    y_train = np.array(Y)

    clf = RandomForestClassifier(n_estimators=50)
    clf.fit(x_train, y_train)

    return clf


def evaluate(model, X, Y):
    """Evaluates the accuracy of a given model on a provided test set"""

    x_test = np.array(X)
    y_test = np.array(Y)

    y_pred = model.predict(x_test)
    print(f"Model Accuracy: {accuracy_score(y_test, y_pred) * 100}%") 

    for i in range(len(y_test)):
        if y_pred[i] != y_test[i]:
            print(f"Window {i} was labeled {y_pred[i]} when it should've been labeled {y_test[i]}")
            print(x_test[i])

    return y_pred
