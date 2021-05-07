import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.neighbors import KNeighborsClassifier
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score
from sklearn.metrics import f1_score


def train_knn(X, Y, k=1):
    """Trains a K-nearest-neighbors classifier on data X and labels Y, and returns the classifier"""
    x_train = np.array(X)
    y_train = np.array(Y)

    clf = KNeighborsClassifier(n_neighbors = 1) # originally 1
    clf.fit(x_train, y_train)

    return clf


def train_forest(X, Y, est=50):
    """Trains a Random Forest classifier on data X and labels Y, and returns the classifier"""

    x_train = np.array(X)
    y_train = np.array(Y)

    clf = RandomForestClassifier(n_estimators=50) # originally 50
    clf.fit(x_train, y_train)

    return clf


def evaluate(model, X, Y, label, verbose=False):
    """Evaluates the accuracy of a given model on a provided test set"""

    x_test = np.array(X)
    y_test = np.array(Y)

    y_pred = model.predict(x_test)

    model_string = type(model).__name__.split(".")[-1]

    print(f"{model_string} Accuracy, Window Length {label}: {accuracy_score(y_test, y_pred) * 100}%") 
    print(f"{model_string} Macro F1, Window Length {label}: {f1_score(y_test, y_pred, average='macro')}") 
    # print(f"{model_string} Micro F1-score with Window Length {label}: {f1_score(y_test, y_pred, average='micro')}") # ends up being same as average

    if verbose:
        for i in range(len(y_test)):
            if y_pred[i] != y_test[i]:
                print(f"Window {i} was labeled {y_pred[i]} when it should've been labeled {y_test[i]}")
                print(x_test[i])

    return y_pred
