from django.shortcuts import render
from django.http import HttpResponse  # 请求和响应API

# Create your views here.
def index(request,methods = ['GET','POST']):
    return render(request, 'index.html')